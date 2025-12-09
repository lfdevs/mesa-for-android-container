/*
 * Copyright © 2025 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"
#include "nir_builder.h"

typedef struct {
   const nir_opt_uub_options *options;
   nir_shader *shader;
   struct hash_table *range_ht;
} opt_uub_state;

static uint32_t
uub(opt_uub_state *state, nir_scalar s)
{
   return nir_unsigned_upper_bound(state->shader, state->range_ht, s);
}

static void
get_srcs(nir_alu_instr *alu, nir_scalar *srcs)
{
   assert(alu->def.num_components == 1);
   nir_scalar def = nir_get_scalar(&alu->def, 0);

   for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; i++)
      srcs[i] = nir_scalar_chase_alu_src(def, i);
}

static bool
get_src_and_const(nir_alu_instr *alu, nir_scalar *src, nir_scalar *const_src,
                  unsigned *const_src_idx)
{
   assert(nir_op_infos[alu->op].num_inputs == 2);

   nir_scalar srcs[2];
   get_srcs(alu, srcs);

   for (unsigned i = 0; i < 2; i++) {
      if (nir_scalar_is_const(srcs[i])) {
         *const_src = srcs[i];
         *src = srcs[1 - i];

         if (const_src_idx)
            *const_src_idx = i;

         return true;
      }
   }

   return false;
}

/* iand src, mask: if mask is constant with N least significant bits set and
 * uub(src) < 2^N, the iand does nothing and can be removed.
 */
static bool
opt_uub_iand(nir_builder *b, nir_alu_instr *alu, opt_uub_state *state)
{
   assert(alu->op == nir_op_iand);

   nir_scalar src, mask;

   if (!get_src_and_const(alu, &src, &mask, NULL))
      return false;

   unsigned first_0 = ffsll(~nir_scalar_as_uint(mask));
   uint32_t low_mask = (1ull << (first_0 - 1)) - 1;

   if (low_mask == 0)
      return false;

   if (uub(state, src) > low_mask)
      return false;

   b->cursor = nir_after_def(src.def);
   nir_def_replace(&alu->def, nir_mov_scalar(b, src));
   return true;
}

static nir_op
invert_cmp(nir_op op)
{
   switch (op) {
   case nir_op_ult:
      return nir_op_uge;
   case nir_op_uge:
      return nir_op_ult;
   case nir_op_ilt:
      return nir_op_ige;
   case nir_op_ige:
      return nir_op_ilt;
   default:
      UNREACHABLE("unexpected cmp op");
   }
}

/* ult src, const: if uub(src) < cmp -> true
 * uge src, const: if uub(src) < cmp -> false
 * ilt src, const: if uub(src) >= 0 && cmp <  0 -> false
 *                 if uub(src) >= 0 && cmp >= 0 -> ult src, const
 * ige src, const: if uub(src) >= 0 && cmp <  0 -> true
 *                 if uub(src) >= 0 && cmp >= 0 -> uge src, const
 */
static bool
opt_uub_cmp(nir_builder *b, nir_alu_instr *alu, opt_uub_state *state)
{
   assert(alu->op == nir_op_ult || alu->op == nir_op_uge ||
          alu->op == nir_op_ilt || alu->op == nir_op_ige);

   if (nir_src_bit_size(alu->src[0].src) > 32)
      return false;

   nir_scalar src, cmp;
   unsigned const_src_idx;

   if (!get_src_and_const(alu, &src, &cmp, &const_src_idx))
      return false;

   uint32_t src_uub = uub(state, src);

   /* To make the code below more uniform, make sure the constant is always
    * the RHS operand by inverting the opcode when it isn't.
    */
   nir_op op = const_src_idx == 0 ? invert_cmp(alu->op) : alu->op;

   if (op == nir_op_ilt || op == nir_op_ige) {
      /* If src could be negative, there's nothing we can prove. */
      if (util_sign_extend(src_uub, src.def->bit_size) < 0)
         return false;

      if (nir_scalar_as_int(cmp) < 0) {
         /* src >= 0 && cmp < 0: ige -> true, ilt -> false  */
         bool replacement = op == nir_op_ige;
         b->cursor = nir_after_instr(&alu->instr);
         nir_def_replace(&alu->def, nir_imm_bool(b, replacement));
         return true;
      }

      /* src >= 0 && cmp >= 0: same as unsigned cmp. */
      op = op == nir_op_ilt ? nir_op_ult : nir_op_uge;
   }

   if (src_uub >= nir_scalar_as_uint(cmp))
      return false;

   /* Replace ult with true, uge with false. */
   bool replacement = op == nir_op_ult;
   b->cursor = nir_after_instr(&alu->instr);
   nir_def_replace(&alu->def, nir_imm_bool(b, replacement));
   return true;
}

/* umin src, const: if uub(src) <= const -> src
 * umax src, const: if uub(src) <= const -> const
 * imin src, const: if uub(src) >= 0 && const <  0 -> const
 *                  if uub(src) >= 0 && const >= 0 -> umin src, const
 * imax src, const: if uub(src) >= 0 && const <  0 -> src
 *                  if uub(src) >= 0 && const >= 0 -> umax src, const
 */
static bool
opt_uub_minmax(nir_builder *b, nir_alu_instr *alu, opt_uub_state *state)
{
   assert(alu->op == nir_op_umin || alu->op == nir_op_umax ||
          alu->op == nir_op_imin || alu->op == nir_op_imax);

   nir_scalar src, const_src;

   if (!get_src_and_const(alu, &src, &const_src, NULL))
      return false;

   uint32_t src_uub = uub(state, src);
   nir_op op = alu->op;

   if (op == nir_op_imin || op == nir_op_imax) {
      /* If src could be negative, there's nothing we can prove. */
      if (util_sign_extend(src_uub, src.def->bit_size) < 0)
         return false;

      if (nir_scalar_as_int(const_src) < 0) {
         /* src >= 0 && const < 0: imin -> const, imax -> src  */
         nir_scalar replacement = alu->op == nir_op_imin ? const_src : src;
         b->cursor = nir_after_instr(&alu->instr);
         nir_def_replace(&alu->def, nir_mov_scalar(b, replacement));
         return true;
      }

      /* src >= 0 && cmp >= 0: same as umin/umax. */
      op = op == nir_op_imin ? nir_op_umin : nir_op_umax;
   }

   if (src_uub > nir_scalar_as_uint(const_src))
      return false;

   nir_scalar replacement = alu->op == nir_op_umax ? const_src : src;
   b->cursor = nir_after_instr(&alu->instr);
   nir_def_replace(&alu->def, nir_mov_scalar(b, replacement));
   return true;
}

static bool
try_replace_imul(nir_builder *b, nir_alu_instr *alu, nir_scalar *srcs,
                 uint32_t *src_uubs, unsigned bits_used, nir_op op)
{
   uint32_t max = (1 << bits_used) - 1;

   if (src_uubs[0] > max || src_uubs[1] > max)
      return false;

   b->cursor = nir_after_instr(&alu->instr);
   nir_def_replace(&alu->def, nir_build_alu2(b, op, nir_mov_scalar(b, srcs[0]),
                                             nir_mov_scalar(b, srcs[1])));
   return true;
}

/* imul src0, src1: if uub(srci) < UINT16_MAX -> umul_16x16 src0, src1
 * imul src0, src1: if uub(srci) < UINT24_MAX -> umul24 src0, src1
 * imul src0, src1: if uub(srci) < UINT23_MAX -> imul24 src0, src1
 */
static bool
opt_uub_imul(nir_builder *b, nir_alu_instr *alu, opt_uub_state *state)
{
   assert(alu->op == nir_op_imul);

   if (!state->options->opt_imul || alu->def.bit_size != 32)
      return false;

   const nir_shader_compiler_options *shader_options = b->shader->options;

   nir_scalar srcs[2];
   get_srcs(alu, srcs);
   uint32_t src_uubs[] = {uub(state, srcs[0]), uub(state, srcs[1])};

   if (shader_options->has_umul_16x16 &&
       try_replace_imul(b, alu, srcs, src_uubs, 16, nir_op_umul_16x16)) {
      return true;
   }

   if ((shader_options->has_umul24 || shader_options->has_mul24_relaxed) &&
       try_replace_imul(b, alu, srcs, src_uubs, 24,
                        b->shader->options->has_mul24_relaxed
                           ? nir_op_umul24_relaxed
                           : nir_op_umul24)) {
      return true;
   }

   /* imul24 sign-extends its 24-bit sources which may give the wrong result
    * if the unsigned upper bound fits in 24-bits. Check if it fits is 23-bits
    * to avoid sign-extension.
    */
   if ((shader_options->has_imul24 || shader_options->has_mul24_relaxed) &&
       try_replace_imul(b, alu, srcs, src_uubs, 23,
                        b->shader->options->has_mul24_relaxed
                           ? nir_op_imul24_relaxed
                           : nir_op_imul24)) {
      return true;
   }

   return false;
}

static bool
src_is_const(nir_src *src, void *data)
{
   return nir_src_is_const(*src);
}

static bool
opt_uub(nir_builder *b, nir_alu_instr *alu, void *data)
{
   /* nir_unsigned_upper_bound calculates 32-bit upper bounds so ignore 64-bit
    * instructions. Also ignore non-scalar instructions to simplify the code.
    */
   if (alu->def.bit_size > 32 || alu->def.num_components > 1)
      return false;

   /* If all sources are constant, let constant folding handle this. */
   if (nir_foreach_src(&alu->instr, src_is_const, NULL))
      return false;

   opt_uub_state *state = data;

   /* If the upper bound is zero, zero is the only possible value. */
   if (uub(state, nir_get_scalar(&alu->def, 0)) == 0) {
      b->cursor = nir_after_def(&alu->def);
      nir_def_replace(&alu->def, nir_imm_zero(b, 1, alu->def.bit_size));
      return true;
   }

   switch (alu->op) {
   case nir_op_iand:
      return opt_uub_iand(b, alu, state);
   case nir_op_ult:
   case nir_op_uge:
   case nir_op_ilt:
   case nir_op_ige:
      return opt_uub_cmp(b, alu, state);
   case nir_op_umin:
   case nir_op_umax:
   case nir_op_imin:
   case nir_op_imax:
      return opt_uub_minmax(b, alu, state);
   case nir_op_imul:
      return opt_uub_imul(b, alu, state);
   default:
      return false;
   }
}

/* Performs a number of optimizations that make use of
 * nir_unsigned_upper_bound to simplify/remove instructions.
 */
bool
nir_opt_uub(nir_shader *shader, const nir_opt_uub_options *options)
{
   opt_uub_state state = {
      .options = options,
      .shader = shader,
      .range_ht = _mesa_pointer_hash_table_create(NULL),
   };

   bool progress =
      nir_shader_alu_pass(shader, opt_uub, nir_metadata_control_flow, &state);

   _mesa_hash_table_destroy(state.range_ht, NULL);
   return progress;
}
