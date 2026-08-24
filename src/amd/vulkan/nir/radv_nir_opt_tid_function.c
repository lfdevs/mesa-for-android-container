/*
 * Copyright 2023 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "nir_builder.h"
#include "nir_constant_expressions.h"
#include "radv_nir.h"

/* This pass optimizes shuffles and boolean alu where the source can be
 * expressed as a function of tid (only subgroup_invocation_id, local_invocation_index,
 * local_invocation_id or constant as inputs).
 * Shuffles are replaced by specialized intrinsics, boolean alu by inverse_ballot.
 * The pass first computes the function of tid (fotid) mask, and then uses constant
 * folding to compute the source for each invocation.
 *
 * This pass assumes that local_invocation_index = subgroup_id * subgroup_size + subgroup_invocation_id.
 * That is not guaranteed by the VK spec, but it's how amd hardware works, if the GFX12 INTERLEAVE_BITS_X/Y
 * fields are not used. This is also the main reason why this pass is currently radv specific.
 */

#define NIR_MAX_SUBGROUP_SIZE     128
#define NIR_MAX_SUBGROUP_BITSET_WORDS BITSET_WORDS(NIR_MAX_SUBGROUP_SIZE)
#define FOTID_MAX_RECURSION_DEPTH 16 /* totally arbitrary */

static inline unsigned
src_get_fotid_mask(nir_src src)
{
   return nir_def_instr(src.ssa)->pass_flags;
}

static inline unsigned
alu_src_get_fotid_mask(nir_alu_instr *instr, unsigned idx)
{
   unsigned unswizzled = src_get_fotid_mask(instr->src[idx].src);
   unsigned result = 0;
   for (unsigned i = 0; i < nir_ssa_alu_instr_src_components(instr, idx); i++) {
      bool is_fotid = unswizzled & (1u << instr->src[idx].swizzle[i]);
      result |= is_fotid << i;
   }
   return result;
}

static void
update_fotid_alu(nir_builder *b, nir_alu_instr *instr, const radv_nir_opt_tid_function_options *options)
{
   const nir_op_info *info = &nir_op_infos[instr->op];

   unsigned res = BITFIELD_MASK(instr->def.num_components);
   for (unsigned i = 0; res != 0 && i < info->num_inputs; i++) {
      unsigned src_mask = alu_src_get_fotid_mask(instr, i);
      if (info->input_sizes[i] == 0)
         res &= src_mask;
      else if (src_mask != BITFIELD_MASK(info->input_sizes[i]))
         res = 0;
   }

   instr->instr.pass_flags = (uint8_t)res;

   /* Special case iand(local_index, #c) or iand(local_id.x, #c) where
    * c < subgroup_size.
    */
   if (instr->op == nir_op_iand && !instr->instr.pass_flags &&
       b->shader->info.derivative_group != DERIVATIVE_GROUP_QUADS) {
      nir_scalar src[2] = {
         nir_scalar_resolved(instr->src[0].src.ssa, instr->src[0].swizzle[0]),
         nir_scalar_resolved(instr->src[1].src.ssa, instr->src[1].swizzle[0]),
      };

      for (unsigned i = 0; i < 2; i++) {
         if (!nir_scalar_is_const(src[i]) || nir_scalar_as_uint(src[i]) >= b->shader->info.max_subgroup_size)
            continue;

         if (!nir_scalar_is_intrinsic(src[!i]))
            continue;

         switch (nir_scalar_intrinsic_op(src[!i])) {
         case nir_intrinsic_load_local_invocation_index:
            break;
         case nir_intrinsic_load_local_invocation_id:
            /* workgroup_size.x must be a power of two, otherwise the iand
             * won't produce the same result for all subgroups.
             */
            if (src[!i].comp != 0 || b->shader->info.workgroup_size_variable)
               continue;
            if (!util_is_power_of_two_nonzero(b->shader->info.workgroup_size[0]))
               continue;
            break;
         default:
            continue;
         }

         instr->instr.pass_flags = 0x1;
         break;
      }
   }
}

static void
update_fotid_intrinsic(nir_builder *b, nir_intrinsic_instr *instr, const radv_nir_opt_tid_function_options *options)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_load_subgroup_invocation: {
      instr->instr.pass_flags = 1;
      break;
   }
   case nir_intrinsic_load_local_invocation_id: {
      if (b->shader->info.workgroup_size_variable)
         break;

      /* subgroup_invocation_id <-> local_id mapping is not strictly defined by
       * the spec.  We assume linear dispatch, and with DERIVATIVE_GROUP_QUADS
       * linear dispatch of quads.
       */
      unsigned partial_size = 1;
      for (unsigned i = 0; i < 3; i++) {
         partial_size *= b->shader->info.workgroup_size[i];

         const bool quad_x = i == 0 && b->shader->info.derivative_group == DERIVATIVE_GROUP_QUADS;
         if (partial_size * (quad_x ? 2 : 1) <= b->shader->info.max_subgroup_size &&
             util_is_power_of_two_nonzero(partial_size)) {
            instr->instr.pass_flags = (uint8_t)BITFIELD_MASK(i + 1);
         }
      }
      if (partial_size <= b->shader->info.max_subgroup_size)
         instr->instr.pass_flags = 0x7;
      break;
   }
   case nir_intrinsic_load_local_invocation_index: {
      assert(b->shader->info.derivative_group != DERIVATIVE_GROUP_QUADS);
      if (b->shader->info.workgroup_size_variable)
         break;
      unsigned workgroup_size =
         b->shader->info.workgroup_size[0] * b->shader->info.workgroup_size[1] * b->shader->info.workgroup_size[2];
      if (workgroup_size <= b->shader->info.max_subgroup_size)
         instr->instr.pass_flags = 0x1;
      break;
   }
   case nir_intrinsic_inverse_ballot: {
      if (src_get_fotid_mask(instr->src[0]) == BITFIELD_MASK(instr->src[0].ssa->num_components)) {
         instr->instr.pass_flags = 0x1;
      }
      break;
   }
   default: {
      break;
   }
   }
}

static void
update_fotid_load_const(nir_load_const_instr *instr)
{
   instr->instr.pass_flags = (uint8_t)BITFIELD_MASK(instr->def.num_components);
}

static bool
update_fotid_instr(nir_builder *b, nir_instr *instr, const radv_nir_opt_tid_function_options *options)
{
   /* Gather a mask of components that are functions of tid. */
   instr->pass_flags = 0;

   switch (instr->type) {
   case nir_instr_type_alu:
      update_fotid_alu(b, nir_instr_as_alu(instr), options);
      break;
   case nir_instr_type_intrinsic:
      update_fotid_intrinsic(b, nir_instr_as_intrinsic(instr), options);
      break;
   case nir_instr_type_load_const:
      update_fotid_load_const(nir_instr_as_load_const(instr));
      break;
   default:
      break;
   }

   return false;
}

static bool
constant_fold_scalar(nir_scalar s, unsigned invocation_id, nir_shader *shader, nir_const_value *dest, unsigned depth)
{
   if (depth > FOTID_MAX_RECURSION_DEPTH)
      return false;

   memset(dest, 0, sizeof(*dest));

   if (nir_scalar_is_alu(s)) {
      nir_alu_instr *alu = nir_def_as_alu(s.def);
      nir_const_value sources[NIR_ALU_MAX_INPUTS][NIR_MAX_VEC_COMPONENTS];
      const nir_op_info *op_info = &nir_op_infos[alu->op];

      unsigned bit_size = 0;
      if (!nir_alu_type_get_type_size(op_info->output_type))
         bit_size = alu->def.bit_size;

      for (unsigned i = 0; i < op_info->num_inputs; i++) {
         if (!bit_size && !nir_alu_type_get_type_size(op_info->input_types[i]))
            bit_size = alu->src[i].src.ssa->bit_size;

         unsigned offset = 0;
         unsigned num_comp = op_info->input_sizes[i];
         if (num_comp == 0) {
            num_comp = 1;
            offset = s.comp;
         }

         for (unsigned j = 0; j < num_comp; j++) {
            nir_scalar src_scalar = nir_get_scalar(alu->src[i].src.ssa, alu->src[i].swizzle[offset + j]);
            if (!constant_fold_scalar(src_scalar, invocation_id, shader, &sources[i][j], depth + 1))
               return false;
         }
      }

      if (!bit_size)
         bit_size = 32;

      unsigned exec_mode = shader->info.float_controls_execution_mode;

      nir_const_value *srcs[NIR_ALU_MAX_INPUTS];
      for (unsigned i = 0; i < op_info->num_inputs; ++i)
         srcs[i] = sources[i];
      nir_const_value dests[NIR_MAX_VEC_COMPONENTS];
      if (op_info->output_size == 0) {
         nir_eval_const_opcode(alu->op, dests, NULL, 1, bit_size, srcs, exec_mode);
         *dest = dests[0];
      } else {
         nir_eval_const_opcode(alu->op, dests, NULL, s.def->num_components, bit_size, srcs, exec_mode);
         *dest = dests[s.comp];
      }
      return true;
   } else if (nir_scalar_is_intrinsic(s)) {
      switch (nir_scalar_intrinsic_op(s)) {
      case nir_intrinsic_load_subgroup_invocation:
      case nir_intrinsic_load_local_invocation_index: {
         *dest = nir_const_value_for_uint(invocation_id, s.def->bit_size);
         return true;
      }
      case nir_intrinsic_load_local_invocation_id: {
         const unsigned size_x = shader->info.workgroup_size[0];
         const unsigned size_y = shader->info.workgroup_size[1];
         unsigned local_ids[3];

         if (shader->info.derivative_group == DERIVATIVE_GROUP_QUADS) {
            /* x = (invocation_id / 4 * 2 + invocation_id % 2) % block_width */
            const unsigned quad_x = invocation_id / 4 * 2;
            const unsigned quad_sub_x = invocation_id % 2;
            local_ids[0] = (quad_x + quad_sub_x) % size_x;

            /* y = (invocation_id / block_width / 2 * 2 + (invocation_id / 2) % 2) % block_height */
            const unsigned quad_y = invocation_id / size_x / 2 * 2;
            const unsigned quad_sub_y = (invocation_id / 2) % 2;
            local_ids[1] = (quad_y + quad_sub_y) % size_y;
         } else {
            const unsigned xy = invocation_id % (size_x * size_y);
            local_ids[0] = xy % size_x;
            local_ids[1] = xy / size_x;
         }

         local_ids[2] = invocation_id / (size_x * size_y);
         *dest = nir_const_value_for_uint(local_ids[s.comp], s.def->bit_size);
         return true;
      }
      case nir_intrinsic_inverse_ballot: {
         nir_def *src = nir_def_as_intrinsic(s.def)->src[0].ssa;
         unsigned comp = invocation_id / src->bit_size;
         unsigned bit = invocation_id % src->bit_size;
         if (!constant_fold_scalar(nir_get_scalar(src, comp), invocation_id, shader, dest, depth + 1))
            return false;
         uint64_t ballot = nir_const_value_as_uint(*dest, src->bit_size);
         *dest = nir_const_value_for_bool(ballot & (1ull << bit), 1);
         return true;
      }
      default:
         break;
      }
   } else if (nir_scalar_is_const(s)) {
      *dest = nir_scalar_as_const_value(s);
      return true;
   }

   UNREACHABLE("unhandled scalar type");
   return false;
}

static bool
bool_scalar_to_ballot(nir_scalar s, nir_shader *shader, BITSET_WORD *mask)
{
   assert(s.def->bit_size == 1);

   if ((nir_def_instr(s.def)->pass_flags & BITFIELD_BIT(s.comp)) == 0)
      return false;

   for (unsigned i = 0; i < shader->info.max_subgroup_size; i++) {
      nir_const_value value;
      if (!constant_fold_scalar(s, i, shader, &value, 0))
         return false;

      if (nir_const_value_as_bool(value, 1))
         BITSET_SET(mask, i);
      else
         BITSET_CLEAR(mask, i);
   }

   return true;
}

struct fotid_context {
   const radv_nir_opt_tid_function_options *options;
   nir_builder b;
   BITSET_WORD *used_invocations;
   unsigned words_per_def;
};

static void
init_fotid_context(struct fotid_context *ctx, nir_function_impl *impl, const radv_nir_opt_tid_function_options *options)
{
   ctx->options = options;
   ctx->b = nir_builder_create(impl);
   ctx->words_per_def = BITSET_WORDS(impl->function->shader->info.max_subgroup_size);
   ctx->used_invocations = rzalloc_array(NULL, BITSET_WORD, ctx->words_per_def * impl->ssa_alloc);
}

static void
destroy_fotid_context(struct fotid_context *ctx)
{
   ralloc_free(ctx->used_invocations);
}

static BITSET_WORD *
def_used_invocations(struct fotid_context *ctx, nir_def *def)
{
   unsigned offset = def->index * ctx->words_per_def;
   return &ctx->used_invocations[offset];
}

static void
src_mark_used(struct fotid_context *ctx, nir_src *src, const BITSET_WORD *used)
{
   BITSET_WORD *def_used = def_used_invocations(ctx, src->ssa);

   __bitset_or(def_used, def_used, used, ctx->words_per_def);
}

struct shuffle_info {
   struct fotid_context *ctx;
   uint8_t src_invoc[NIR_MAX_SUBGROUP_SIZE];
   nir_shader *shader;
};

static bool
gather_read_invocation_shuffle(nir_intrinsic_instr *intrin, struct shuffle_info *shuffle)
{
   nir_scalar s = nir_get_scalar(intrin->src[1].ssa, 0);

   BITSET_WORD *def_used = def_used_invocations(shuffle->ctx, &intrin->def);

   /* Recursive constant folding for each invocation */
   for (unsigned i = 0; i < shuffle->shader->info.max_subgroup_size; i++) {
      if (!BITSET_TEST(def_used, i)) {
         shuffle->src_invoc[i] = UINT8_MAX;
         continue;
      }

      nir_const_value value;
      if (!constant_fold_scalar(s, i, shuffle->shader, &value, 0))
         return false;
      shuffle->src_invoc[i] = MIN2(nir_const_value_as_uint(value, s.def->bit_size), UINT8_MAX);
   }

   return true;
}

static nir_alu_instr *
get_singluar_user_bcsel(nir_def *def, unsigned *src_idx)
{
   if (def->num_components != 1 || !list_is_singular(&def->uses))
      return NULL;

   nir_alu_instr *bcsel = NULL;
   nir_foreach_use_including_if_safe (src, def) {
      if (nir_src_is_if(src) || nir_src_use_instr(src)->type != nir_instr_type_alu)
         return NULL;
      bcsel = nir_instr_as_alu(nir_src_use_instr(src));
      if (bcsel->op != nir_op_bcsel || bcsel->def.num_components != 1)
         return NULL;
      *src_idx = list_entry(src, nir_alu_src, src) - bcsel->src;
      break;
   }
   assert(*src_idx < 3);

   if (*src_idx == 0)
      return NULL;
   return bcsel;
}

static nir_def *
try_opt_bitwise_mask(nir_builder *b, nir_def *def, struct shuffle_info *shuffle)
{
   unsigned one = NIR_MAX_SUBGROUP_SIZE - 1;
   unsigned zero = NIR_MAX_SUBGROUP_SIZE - 1;
   unsigned copy = NIR_MAX_SUBGROUP_SIZE - 1;
   unsigned invert = NIR_MAX_SUBGROUP_SIZE - 1;

   /* Because we prefer non-zero and_mask we need to special case
    * broadcasts. Otherwise we might end up not matching if only
    * a few invocations matter.
    */
   bool all_equal = true;
   int first_valid = -1;

   for (unsigned i = 0; i < shuffle->shader->info.max_subgroup_size; i++) {
      unsigned read = shuffle->src_invoc[i];
      if (read >= shuffle->shader->info.max_subgroup_size)
         continue; /* undefined result */

      if (first_valid < 0)
         first_valid = i;
      else if (read != shuffle->src_invoc[first_valid])
         all_equal = false;

      copy &= ~(read ^ i);
      invert &= read ^ i;
      one &= read;
      zero &= ~read;
   }

   /* We didn't find valid masks for at least one bit. */
   if ((copy | zero | one | invert) != NIR_MAX_SUBGROUP_SIZE - 1)
      return NULL;

   unsigned and_mask = copy | invert;
   unsigned xor_mask = (one | invert) & ~copy;

#if 0
   fprintf(stderr, "and %x, xor %x \n", and_mask, xor_mask);

   assert(false);
#endif

   if (all_equal && first_valid < 0) {
      return nir_undef(b, def->num_components, def->bit_size);
   } else if (and_mask == 0x7f && xor_mask == 0) {
      return def;
   } else if (shuffle->ctx->options->use_quad_swap_broadcast && and_mask == 0x7f && xor_mask == 0x1) {
      return nir_quad_swap_horizontal(b, def);
   } else if (shuffle->ctx->options->use_quad_swap_broadcast && and_mask == 0x7f && xor_mask == 0x2) {
      return nir_quad_swap_vertical(b, def);
   } else if (shuffle->ctx->options->use_quad_swap_broadcast && and_mask == 0x7f && xor_mask == 0x3) {
      return nir_quad_swap_diagonal(b, def);
   } else if (shuffle->ctx->options->use_quad_swap_broadcast && and_mask == 0x7c && xor_mask <= 0x3) {
      return nir_quad_broadcast(b, def, nir_imm_int(b, xor_mask));
   } else if (shuffle->ctx->options->use_shuffle_xor && and_mask == 0x7f) {
      return nir_shuffle_xor(b, def, nir_imm_int(b, xor_mask));
   } else if (shuffle->ctx->options->use_masked_swizzle_amd && (and_mask & 0x60) == 0x60 && xor_mask <= 0x1f) {
      return nir_masked_swizzle_amd(b, def, (xor_mask << 10) | (and_mask & 0x1f), .fetch_inactive = true);
   } else if (all_equal) {
      /* Oddly enough, we do this last. This is because of there is a DPP pattern,
       * we should prefer it - after all, DPP can be fused into VALU, but not readlane.
       */
      return nir_read_invocation(b, def, nir_imm_int(b, shuffle->src_invoc[first_valid]));
   }

   return NULL;
}

static nir_def *
try_opt_rotate(nir_builder *b, nir_def *def, struct shuffle_info *shuffle)
{
   for (unsigned csize = 4; csize <= shuffle->shader->info.max_subgroup_size; csize *= 2) {
      unsigned cmask = csize - 1;

      unsigned delta = UINT_MAX;
      for (unsigned i = 0; i < shuffle->shader->info.max_subgroup_size; i++) {
         if (shuffle->src_invoc[i] >= shuffle->shader->info.max_subgroup_size)
            continue;

         if (shuffle->src_invoc[i] >= i)
            delta = shuffle->src_invoc[i] - i;
         else
            delta = csize - i + shuffle->src_invoc[i];
         break;
      }

      if (delta >= csize || delta == 0)
         continue;

      bool use_rotate = true;
      for (unsigned i = 0; use_rotate && i < shuffle->shader->info.max_subgroup_size; i++) {
         if (shuffle->src_invoc[i] >= shuffle->shader->info.max_subgroup_size)
            continue;
         use_rotate &= (((i + delta) & cmask) + (i & ~cmask)) == shuffle->src_invoc[i];
      }

      if (use_rotate)
         return nir_rotate(b, def, nir_imm_int(b, delta), .cluster_size = csize);
   }

   return NULL;
}

static nir_def *
try_opt_dpp16_shift(nir_builder *b, nir_intrinsic_instr *intrin, struct shuffle_info *shuffle)
{
   unsigned shuffle_idx = 0;
   nir_alu_instr *bcsel = get_singluar_user_bcsel(&intrin->def, &shuffle_idx);

   if (!bcsel || !nir_src_is_const(bcsel->src[3 - shuffle_idx].src) ||
       nir_src_as_uint(bcsel->src[3 - shuffle_idx].src) != 0)
      return NULL;

   nir_scalar s = nir_get_scalar(bcsel->src[0].src.ssa, bcsel->src[0].swizzle[0]);

   BITSET_WORD reads_zero[NIR_MAX_SUBGROUP_BITSET_WORDS] = {0};

   if (!bool_scalar_to_ballot(s, shuffle->shader, reads_zero))
      return NULL;

   if (shuffle_idx == 1)
      BITSET_NOT(reads_zero);

   int delta = INT_MAX;
   for (unsigned i = 0; i < shuffle->shader->info.max_subgroup_size; i++) {
      if (shuffle->src_invoc[i] >= shuffle->shader->info.max_subgroup_size)
         continue;
      delta = shuffle->src_invoc[i] - i;
      break;
   }

   if (delta < -15 || delta > 15 || delta == 0)
      return NULL;

   for (unsigned i = 0; i < shuffle->shader->info.max_subgroup_size; i++) {
      int read = i + delta;
      bool out_of_bounds = (read & ~0xf) != (i & ~0xf);
      if (BITSET_TEST(reads_zero, i) && !out_of_bounds)
         return NULL;
      if (shuffle->src_invoc[i] >= shuffle->shader->info.max_subgroup_size)
         continue;
      if (read != shuffle->src_invoc[i] || out_of_bounds)
         return NULL;
   }

   nir_def *res = nir_dpp16_shift_amd(b, intrin->src[0].ssa, .base = delta);

   b->cursor = nir_after_instr(&bcsel->instr);
   nir_def_rewrite_uses_with_alu_src(b, &bcsel->def, bcsel->src[shuffle_idx]);
   nir_instr_remove(&bcsel->instr);

   return res;
}

static nir_def *
try_opt_permute(nir_builder *b, nir_def *def, struct shuffle_info *shuffle)
{
   unsigned cluster_sizes = 0;
   if (shuffle->ctx->options->use_masked_swizzle_amd)
      cluster_sizes |= BITFIELD_BIT(4);
   if (shuffle->ctx->options->use_dpp8_swizzle_amd)
      cluster_sizes |= BITFIELD_BIT(8);
   if (shuffle->ctx->options->use_permute16_amd)
      cluster_sizes |= BITFIELD_BIT(16);

   u_foreach_bit (csize, cluster_sizes) {
      uint8_t permute[16];
      memset(permute, 0xff, sizeof(permute));

      bool any_valid = false;
      bool valid_permute = true;
      bool x16 = false;

      for (unsigned i = 0; i < shuffle->shader->info.max_subgroup_size; i++) {
         unsigned read = shuffle->src_invoc[i];
         if (read >= shuffle->shader->info.max_subgroup_size)
            continue;

         if (!any_valid && csize == 16)
            x16 = (read & 0x10) != (i & 0x10);

         if (x16)
            read ^= 0x10;

         unsigned cmask = csize - 1;
         bool oob = (read & ~cmask) != (i & ~cmask);
         bool match = permute[i & cmask] == 0xff || permute[i & cmask] == (read & cmask);

         if (oob || !match) {
            valid_permute = false;
            break;
         }

         permute[i & cmask] = (read & cmask);
         any_valid = true;
      }

      if (!valid_permute)
         continue;

      uint64_t swizzle_mask = 0;
      for (unsigned i = 0; i < csize; i++) {
         if (permute[i] == 0xff)
            continue;
         assert(permute[i] < csize);
         swizzle_mask |= (uint64_t)permute[i] << (i * (ffs(csize) - 1));
      }

      switch (csize) {
      case 4:
         return nir_quad_swizzle_amd(b, def, swizzle_mask, .fetch_inactive = true);
      case 8:
         return nir_dpp8_swizzle_amd(b, def, swizzle_mask);
      case 16: {
         nir_def *sel_lo = nir_imm_int(b, (uint32_t)swizzle_mask);
         nir_def *sel_hi = nir_imm_int(b, (uint32_t)(swizzle_mask >> 32));
         if (x16)
            return nir_lane_permute_x16_amd(b, def, sel_lo, sel_hi);
         else
            return nir_lane_permute_16_amd(b, def, sel_lo, sel_hi);
      }
      default:
         UNREACHABLE("unknown permute size");
      }
   }

   return NULL;
}

static bool
init_fotid_shuffle(struct fotid_context *ctx, nir_intrinsic_instr *instr, struct shuffle_info *shuffle)
{
   if (!nir_def_instr(instr->src[1].ssa)->pass_flags)
      return false;

   *shuffle = (struct shuffle_info){
      .ctx = ctx,
      .shader = ctx->b.shader,
   };

   memset(shuffle->src_invoc, 0xff, sizeof(shuffle->src_invoc));

   return gather_read_invocation_shuffle(instr, shuffle);
}

static void
mark_shuffle_data_used(nir_intrinsic_instr *instr, struct shuffle_info *shuffle)
{
   BITSET_WORD *src_used = def_used_invocations(shuffle->ctx, instr->src[0].ssa);

   for (unsigned i = 0; i < shuffle->shader->info.max_subgroup_size; i++) {
      unsigned read = shuffle->src_invoc[i];
      if (read >= shuffle->shader->info.max_subgroup_size)
         continue;

      BITSET_SET(src_used, read);
   }
}

static bool
opt_fotid_shuffle(nir_intrinsic_instr *instr, struct shuffle_info *shuffle)
{
   mark_shuffle_data_used(instr, shuffle);

   if (nir_src_is_const(instr->src[1]))
      return false; /* Leave obvious broadcasts alone */

#if 0
   for (int i = 0; i < b->shader->info.max_subgroup_size; i++) {
      fprintf(stderr, "invocation %d reads %d\n", i, shuffle->src_invoc[i]);
   }
#endif

   nir_builder *b = &shuffle->ctx->b;
   b->cursor = nir_after_instr(&instr->instr);

   nir_def *res = NULL;

   if (shuffle->ctx->options->use_dpp16_shift_amd)
      res = try_opt_dpp16_shift(b, instr, shuffle);

   if (!res)
      res = try_opt_bitwise_mask(b, instr->src[0].ssa, shuffle);
   if (!res && shuffle->ctx->options->use_clustered_rotate)
      res = try_opt_rotate(b, instr->src[0].ssa, shuffle);
   if (!res)
      res = try_opt_permute(b, instr->src[0].ssa, shuffle);

   if (res) {
      nir_def_replace(&instr->def, res);
      return true;
   } else {
      return false;
   }
}

static bool
opt_fotid_bool(nir_builder *b, nir_alu_instr *instr, const radv_nir_opt_tid_function_options *options)
{
   nir_scalar s = nir_get_scalar(&instr->def, 0);

   BITSET_WORD ballot_set[NIR_MAX_SUBGROUP_BITSET_WORDS] = {0};

   if (!bool_scalar_to_ballot(s, b->shader, ballot_set))
      return false;

   b->cursor = nir_after_instr(&instr->instr);

   nir_const_value ballot_comp[NIR_MAX_VEC_COMPONENTS];

   for (unsigned i = 0; i < options->hw_ballot_num_comp; i++) {
      unsigned bit_start = i * options->hw_ballot_bit_size;

      uint64_t imm = BITSET_EXTRACT64(ballot_set, bit_start, options->hw_ballot_bit_size);

      ballot_comp[i] = nir_const_value_for_uint(imm, options->hw_ballot_bit_size);
   }

   nir_def *ballot = nir_build_imm(b, options->hw_ballot_num_comp, options->hw_ballot_bit_size, ballot_comp);
   nir_def *res = nir_inverse_ballot(b, ballot);
   nir_def_instr(res)->pass_flags = 1;

   nir_def_replace(&instr->def, res);
   return true;
}

struct fotid_init_state {
   const radv_nir_opt_tid_function_options *options;
   bool needs_second_pass;
};

static bool
init_fotid_mask(nir_builder *b, nir_instr *instr, void *params)
{
   struct fotid_init_state *state = params;
   update_fotid_instr(b, instr, state->options);

   switch (instr->type) {
   case nir_instr_type_alu: {
      nir_alu_instr *alu = nir_instr_as_alu(instr);
      if (alu->def.bit_size != 1 || !instr->pass_flags)
         return false;

      state->needs_second_pass = true;

      if (!state->options->hw_ballot_bit_size || !state->options->hw_ballot_num_comp)
         return false;
      if (alu->def.num_components > 1)
         return false;
      return opt_fotid_bool(b, alu, state->options);
   }
   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      switch (intrin->intrinsic) {
      case nir_intrinsic_shuffle:
      case nir_intrinsic_read_invocation:
         state->needs_second_pass |= src_get_fotid_mask(intrin->src[1]);
         break;
      case nir_intrinsic_inverse_ballot:
         state->needs_second_pass |= nir_src_is_const(intrin->src[0]);
         break;
      default:
         break;
      }
      return false;
   }
   default:
      return false;
   }
}

struct src_mark_used_data {
   struct fotid_context *ctx;
   const BITSET_WORD *used;
};

static bool
src_mark_used_cb(nir_src *src, void *_data)
{
   struct src_mark_used_data *data = _data;

   src_mark_used(data->ctx, src, data->used);

   return true;
}

enum cond_restriction {
   THEN_RESTRICTED = 0x1,
   ELSE_RESTRICTED = 0x2,
};

static uint8_t
get_restriction(nir_scalar *cond)
{
   /* Look through one level of iand/ior for one source that restricts maybe active invocations. */
   if ((nir_def_instr(cond->def)->pass_flags & BITFIELD_BIT(cond->comp)) == 0 && nir_scalar_is_alu(*cond)) {
      nir_op op = nir_scalar_alu_op(*cond);
      if (op == nir_op_iand || op == nir_op_ior) {
         for (unsigned i = 0; i < 2; i++) {
            nir_scalar alu_src = nir_scalar_chase_alu_src(*cond, i);
            if ((nir_def_instr(alu_src.def)->pass_flags & BITFIELD_BIT(alu_src.comp)) != 0) {
               *cond = alu_src;
               return op == nir_op_iand ? THEN_RESTRICTED : ELSE_RESTRICTED;
            }
         }
      }
   }

   return THEN_RESTRICTED | ELSE_RESTRICTED;
}

static bool
opt_fotid_instr(nir_instr *instr, struct fotid_context *ctx, const BITSET_WORD *maybe_active)
{
   switch (instr->type) {
   case nir_instr_type_alu: {
      nir_alu_instr *alu = nir_instr_as_alu(instr);

      BITSET_WORD *def_used = def_used_invocations(ctx, &alu->def);
      __bitset_and(def_used, def_used, maybe_active, ctx->words_per_def);

      switch (alu->op) {
      case nir_op_bcsel: {
         bool has_cond_swizzle = false;
         for (unsigned i = 1; i < alu->def.num_components; i++)
            has_cond_swizzle |= alu->src[0].swizzle[0] != alu->src[0].swizzle[i];

         if (has_cond_swizzle)
            break;

         BITSET_WORD cond_ballot[NIR_MAX_SUBGROUP_BITSET_WORDS] = {0};
         nir_scalar cond = nir_get_scalar(alu->src[0].src.ssa, alu->src[0].swizzle[0]);
         uint8_t cond_restriction = get_restriction(&cond);

         if (!bool_scalar_to_ballot(cond, ctx->b.shader, cond_ballot))
            break;

         int mov_src = -1;
         for (unsigned i = 1; i < 3; i++) {
            if ((cond_restriction & (i == 1 ? THEN_RESTRICTED : ELSE_RESTRICTED)) == 0) {
               src_mark_used(ctx, &alu->src[i].src, def_used);
               continue;
            }

            BITSET_WORD src_used[NIR_MAX_SUBGROUP_BITSET_WORDS] = {0};
            if (i == 1)
               __bitset_and(src_used, def_used, cond_ballot, ctx->words_per_def);
            else
               __bitset_andnot(src_used, def_used, cond_ballot, ctx->words_per_def);

            if (!BITSET_TEST_COUNT(src_used, 0, ctx->b.shader->info.max_subgroup_size)) {
               mov_src = 3 - i;
            } else {
               src_mark_used(ctx, &alu->src[i].src, src_used);
            }
         }

         if (mov_src > 0) {
            /* Only one of the sources is actually used,
             * turn the bcsel into a move.
             */
            ctx->b.cursor = nir_after_instr(&alu->instr);

            nir_def *mov = nir_mov_alu(&ctx->b, alu->src[mov_src], alu->def.num_components);
            nir_def_replace(&alu->def, mov);

            return true;
         } else {
            src_mark_used(ctx, &alu->src[0].src, def_used);
            return false;
         }
      }
      case nir_op_iand:
      case nir_op_ior: {
         if (alu->def.bit_size != 1 || alu->def.num_components != 1)
            break;

         for (unsigned i = 0; i < 2; i++) {
            nir_scalar cond = nir_get_scalar(alu->src[i].src.ssa, alu->src[i].swizzle[0]);

            BITSET_WORD other_used[NIR_MAX_SUBGROUP_BITSET_WORDS] = {0};

            if (!bool_scalar_to_ballot(cond, ctx->b.shader, other_used))
               continue;

            if (alu->op == nir_op_iand)
               __bitset_and(other_used, def_used, other_used, ctx->words_per_def);
            else
               __bitset_andnot(other_used, def_used, other_used, ctx->words_per_def);

            /* We don't need all invocations of the other source if the current one is
             * an inverse_ballot.
             */
            src_mark_used(ctx, &alu->src[!i].src, other_used);
            src_mark_used(ctx, &alu->src[i].src, def_used);
            return false;
         }

         break;
      }
      default:
         break;
      }

      const nir_op_info *info = &nir_op_infos[alu->op];

      for (unsigned i = 0; i < info->num_inputs; i++)
         src_mark_used(ctx, &alu->src[i].src, def_used);

      return false;
   }
   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      switch (intrin->intrinsic) {
      case nir_intrinsic_read_invocation: {
         if (!nir_src_is_const(intrin->src[1]))
            break;
         unsigned invocation = nir_src_as_uint(intrin->src[1]);
         if (invocation < ctx->b.shader->info.max_subgroup_size) {
            BITSET_WORD *src_used = def_used_invocations(ctx, intrin->src[0].ssa);
            BITSET_SET(src_used, invocation);
         }
         src_mark_used(ctx, &intrin->src[1], maybe_active);
         return false;
      }
      case nir_intrinsic_shuffle: {
         BITSET_WORD *def_used = def_used_invocations(ctx, &intrin->def);
         __bitset_and(def_used, def_used, maybe_active, ctx->words_per_def);

         struct shuffle_info shuffle;
         if (!init_fotid_shuffle(ctx, intrin, &shuffle))
            break;

         if (opt_fotid_shuffle(intrin, &shuffle)) {
            return true;
         } else {
            src_mark_used(ctx, &intrin->src[1], maybe_active);
            return false;
         }
      }
      case nir_intrinsic_inverse_ballot: {
         if (!nir_src_is_const(intrin->src[0]))
            break;

         BITSET_WORD *def_used = def_used_invocations(ctx, &intrin->def);
         __bitset_and(def_used, def_used, maybe_active, ctx->words_per_def);

         BITSET_WORD ballot[NIR_MAX_SUBGROUP_BITSET_WORDS] = {0};

         nir_scalar s = nir_get_scalar(&intrin->def, 0);
         if (!bool_scalar_to_ballot(s, ctx->b.shader, ballot))
            break;

         __bitset_and(ballot, ballot, def_used, ctx->words_per_def);

         bool is_false = __bitset_is_empty(ballot, ctx->words_per_def);
         bool is_true = !is_false && memcmp(ballot, def_used, ctx->words_per_def * sizeof(BITSET_WORD)) == 0;
         if (!is_false && !is_true)
            break;

         ctx->b.cursor = nir_after_instr(&intrin->instr);

         /* Replace inverse_ballot with a constant boolean if it's always true/false in the invocations
          * where it's used.
          */
         nir_def_replace(&intrin->def, nir_imm_bool(&ctx->b, is_true));
         return true;
      }
      default:
         break;
      }
      break;
   }
   default:
      break;
   }

   struct src_mark_used_data data = {
      .ctx = ctx,
      .used = maybe_active,
   };

   nir_foreach_src(instr, src_mark_used_cb, &data);

   return false;
}

static bool
opt_fotid_list(struct exec_list *list, struct fotid_context *ctx, const BITSET_WORD *maybe_active)
{
   bool progress = false;
   foreach_list_typed_reverse (nir_cf_node, node, node, list) {
      switch (node->type) {
      case nir_cf_node_block: {
         nir_block *block = nir_cf_node_as_block(node);
         nir_foreach_instr_reverse_safe (instr, block) {
            if (instr->type == nir_instr_type_phi)
               break; /* Phis are handled in cf_node before them. */
            progress |= opt_fotid_instr(instr, ctx, maybe_active);
         }
         break;
      }
      case nir_cf_node_if: {
         nir_if *nif = nir_cf_node_as_if(node);

         src_mark_used(ctx, &nif->condition, maybe_active);

         nir_scalar cond = nir_scalar_resolved(nif->condition.ssa, 0);
         uint8_t cond_restriction = get_restriction(&cond);

         BITSET_WORD cond_ballot[NIR_MAX_SUBGROUP_BITSET_WORDS] = {0};
         if (!bool_scalar_to_ballot(cond, ctx->b.shader, cond_ballot))
            cond_restriction = 0;

         nir_block *merge = nir_cf_node_as_block(nir_cf_node_next(node));

         for (unsigned visit_then = 0; visit_then < 2; visit_then++) {
            BITSET_WORD branch_active_storage[NIR_MAX_SUBGROUP_BITSET_WORDS];
            const BITSET_WORD *branch_active = maybe_active;
            if (cond_restriction & (visit_then ? THEN_RESTRICTED : ELSE_RESTRICTED)) {
               if (visit_then)
                  __bitset_and(branch_active_storage, maybe_active, cond_ballot, ctx->words_per_def);
               else
                  __bitset_andnot(branch_active_storage, maybe_active, cond_ballot, ctx->words_per_def);
               branch_active = branch_active_storage;
            }

            nir_block *last_block = visit_then ? nir_if_last_then_block(nif) : nir_if_last_else_block(nif);

            if (nir_block_has_pred(merge, last_block)) {
               nir_foreach_phi (phi, merge) {
                  nir_phi_src *phi_src = nir_phi_get_src_from_block(phi, last_block);

                  const BITSET_WORD *phi_used = def_used_invocations(ctx, &phi->def);

                  BITSET_WORD phi_src_used[NIR_MAX_SUBGROUP_BITSET_WORDS];

                  __bitset_and(phi_src_used, phi_used, branch_active, ctx->words_per_def);

                  src_mark_used(ctx, &phi_src->src, phi_src_used);
               }
            }

            progress |= opt_fotid_list(visit_then ? &nif->then_list : &nif->else_list, ctx, branch_active);
         }
         break;
      }
      case nir_cf_node_loop: {
         nir_loop *loop = nir_cf_node_as_loop(node);

         /* Propagate which invocations are used for loop exit phis. */
         nir_foreach_phi (phi, nir_cf_node_as_block(nir_cf_node_next(node))) {
            BITSET_WORD *phi_used = def_used_invocations(ctx, &phi->def);

            __bitset_and(phi_used, phi_used, maybe_active, ctx->words_per_def);

            nir_foreach_phi_src (phi_src, phi) {
               src_mark_used(ctx, &phi_src->src, phi_used);
            }
         }

         /* Handle loop header phi: assume all active invocations are used. */
         nir_foreach_phi (phi, nir_loop_first_block(loop)) {
            nir_foreach_phi_src (phi_src, phi) {
               src_mark_used(ctx, &phi_src->src, maybe_active);
            }
         }

         progress |= opt_fotid_list(&loop->body, ctx, maybe_active);

         break;
      }
      default:
         UNREACHABLE("unknown nf_node type");
      }
   }

   return progress;
}

static bool
opt_fotid_impl(nir_function_impl *impl, const radv_nir_opt_tid_function_options *options)
{
   struct fotid_context ctx = {0};
   init_fotid_context(&ctx, impl, options);

   BITSET_WORD maybe_active[NIR_MAX_SUBGROUP_BITSET_WORDS] = {0};

   BITSET_SET_COUNT(maybe_active, 0, impl->function->shader->info.max_subgroup_size);

   bool progress = opt_fotid_list(&impl->body, &ctx, maybe_active);

   destroy_fotid_context(&ctx);

   return nir_progress(progress, impl, nir_metadata_control_flow);
}

bool
radv_nir_opt_tid_function(nir_shader *shader, const radv_nir_opt_tid_function_options *options)
{
   bool progress = false;

   struct fotid_init_state state = {
      .options = options,
      .needs_second_pass = false,
   };

   /* The pass is split in two steps because the shuffle optimization needs the function of tid mask
    * on instruction that come after the shuffle. The first set also optimizes booleans to inverse_ballot
    * to reduce work during the second step.
    */
   progress |= nir_shader_instructions_pass(shader, init_fotid_mask, nir_metadata_control_flow, &state);

   if (!state.needs_second_pass)
      return progress;

   nir_foreach_function_impl (impl, shader) {
      progress |= opt_fotid_impl(impl, options);
   }

   return progress;
}
