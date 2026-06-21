/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#include "brw_nir.h"
#define XXH_INLINE_ALL
#include "util/xxhash.h"
#include "util/simple_mtx.h"

/**
 * Assumed depth of the systolic array on XeHP+ hardware -- Equal to
 * the number of rows of the right operand of the matrix
 * multiplication.
 */
#define XEHP_SYSTOLIC_DEPTH 8

/**
 * Assumed execution width of the systolic array on XeHP+ hardware --
 * Equal to the number of columns of the right operand of the matrix
 * multiplication.
 */
#define XEHP_SYSTOLIC_WIDTH(devinfo) ((devinfo)->ver >= 20 ? 16 : 8)

/**
 * Maximum number of passes of the systolic array performed by a DPAS
 * instruction, equivalent to the maximum number of rows of the left
 * operand of the matrix multiplication.
 */
#define XEHP_SYSTOLIC_REPS 8

/**
 * Number of bits processed per channel from the A and B sources of
 * the DPAS instructions.  DPAS instructions with scalar types
 * narrower than this calculate the dot product of multiple logical
 * components packed within the same channel.
 */
#define XEHP_SYSTOLIC_CHANNEL_BITS 32

/**
 * Key structure for identifying chains of dot products that can be
 * vectorized.  Contains the operands for \c XEHP_SYSTOLIC_DEPTH
 * chained dot product operations that can be combined into a single
 * DPAS (Dot Product Accumulate Systolic) instruction.
 */
struct dp_key {
   /* Left operand of each dot product intrinsic. */
   nir_def *a[XEHP_SYSTOLIC_DEPTH];
   /* Right operand of each dot product intrinsic. */
   nir_def *b[XEHP_SYSTOLIC_DEPTH];
   /* Right operand component index of each dot product intrinsic. */
   unsigned b_comp[XEHP_SYSTOLIC_DEPTH];
};

/**
 * Entry structure representing a candidate chain of dot products for
 * vectorization.  These entries are grouped by their dp_key to find
 * compatible chains.
 */
struct dp_entry {
   /* Linked list node. */
   struct list_head head;
   /* Key identifying the dot product operands. */
   struct dp_key key;
   /* Component index for the left operand (all intrinsics
    * in a single chain must take the same component of
    * the left operand).
    */
   unsigned a_comp;
   /* First dot product instruction in the chain. */
   nir_def *first;
   /* Last dot product instruction in the chain. */
   nir_def *last;
};

/**
 * Hash function for dp_key structures used in hash table lookups.
 */
static uint32_t
hash_dp_key(const void *_key)
{
   const struct dp_key *key = _key;
   uint32_t hash = 0;
   for (unsigned i = 0; i < ARRAY_SIZE(key->a); i++)
      hash = XXH32(&key->a[i]->index, sizeof(key->a[i]->index), hash);
   for (unsigned i = 0; i < ARRAY_SIZE(key->b); i++)
      hash = XXH32(&key->b[i]->index, sizeof(key->b[i]->index), hash);
   for (unsigned i = 0; i < ARRAY_SIZE(key->b_comp); i++)
      hash = XXH32(&key->b_comp[i], sizeof(key->b_comp[i]), hash);
   return hash;
}

/**
 * Equality function for dp_key structures used in hash table
 * comparisons.
 */
static bool
dp_keys_equal(const void *_key0, const void *_key1)
{
   const struct dp_key *key0 = _key0;
   const struct dp_key *key1 = _key1;

   for (unsigned i = 0; i < ARRAY_SIZE(key0->a); i++)
      if (key0->a[i] != key1->a[i])
         return false;

   for (unsigned i = 0; i < ARRAY_SIZE(key0->b); i++)
      if (key0->b[i] != key1->b[i])
         return false;

   for (unsigned i = 0; i < ARRAY_SIZE(key0->b_comp); i++)
      if (key0->b_comp[i] != key1->b_comp[i])
         return false;

   return true;
}

/**
 * Validate that an instruction is a supported memory load operation.
 * XXX - Allow memory load intrinsics other than SSBOs.
 */
static bool
is_supported_memory_load(const nir_intrinsic_instr *instr)
{
   return (instr->intrinsic == nir_intrinsic_load_ssbo_uniform_block_intel ||
           instr->intrinsic == nir_intrinsic_load_ssbo_intel) &&
          instr->def.bit_size == XEHP_SYSTOLIC_CHANNEL_BITS;
}

/**
 * Check if an ALU instruction represents an additive address
 * expression.  Determines if the instruction computes an integer
 * equal to base plus a constant offset.  The index of the constant
 * source is returned by the \p i output parameter to allow the
 * commuted expression to be handled consistently.
 */
static bool
is_additive_address_expr(const nir_alu_instr *addr,
                         const nir_def *base,
                         unsigned *i)
{
   for (*i = 0; *i < 2; (*i)++) {
      if (addr && addr->op == nir_op_iadd &&
          ((!base || addr->src[1 - *i].src.ssa == base) &&
           nir_src_as_const_value(addr->src[*i].src)))
         return true;
   }

   return false;
}

/**
 * Verify that all left operands in a chain of dot products have
 * consistent memory offsets that increase linearly between \c a[0]
 * and \c a[7], and return the increment between the offsets of
 * adjacent loads in the chain.
 *
 * This ensures that the loads can be safely coalesced into a single
 * vectorized operation that yields the elements in a layout
 * equivalent to the transpose of the original, so it can be used as
 * left operand in a matrix multiplication.
 *
 * Note that because only chains with matching \c a[i] sources are
 * vectorized, it's only necessary to check the offsets of the first
 * dot product chain \p ent0.
 */
static unsigned
stride_of_a_linear_offsets(const struct dp_entry *ent0,
                           const nir_intrinsic_instr *load_a0)
{
   const nir_alu_instr *addr_a0 = nir_def_as_alu_or_null(load_a0->src[1].ssa);
   const nir_intrinsic_instr *load_a1 = nir_def_as_intrinsic(ent0->key.a[1]);
   const nir_alu_instr *addr_a1 = nir_def_as_alu_or_null(load_a1->src[1].ssa);
   unsigned delta = 0;

   for (unsigned i = 1; i < ARRAY_SIZE(ent0->key.a); i++) {
      nir_intrinsic_instr *load_ai = nir_def_as_intrinsic(ent0->key.a[i]);
      const nir_alu_instr *addr_ai = nir_def_as_alu_or_null(load_ai->src[1].ssa);
      unsigned j, k, l;

      /* Case where the linear sequence is specified by the increasing
       * base of the memory loads.
       */
      if (nir_intrinsic_base(load_a1) != nir_intrinsic_base(load_a0)) {
         /* The address source is required to be equal for all loads
          * of the chain.
          */
         if (!(load_ai->src[1].ssa == load_a0->src[1].ssa ||
               (nir_src_as_const_value(load_ai->src[1]) &&
                nir_src_as_const_value(load_a0->src[1]) &&
                nir_src_as_const_value(load_ai->src[1])[0].u32 == nir_src_as_const_value(load_a0->src[1])[0].u32)))
            return 0;

         if (i == 1)
            delta = nir_intrinsic_base(load_a1) - nir_intrinsic_base(load_a0);
         else if (nir_intrinsic_base(load_ai) != nir_intrinsic_base(load_a0) + delta * i)
            return 0;

      /* Case where the linear sequence is specified by a sequence of
       * additive address expressions with base equal to the first
       * address in the chain.
       */
      } else if (is_additive_address_expr(addr_a1, load_a0->src[1].ssa, &j)) {
         if (i == 1)
            delta = nir_src_as_const_value(addr_a1->src[j].src)->u32;
         else if (!is_additive_address_expr(addr_ai, load_a0->src[1].ssa, &l) ||
                  nir_src_as_const_value(addr_ai->src[l].src)->u32 != delta * i)
            return 0;

      /* Case where the linear sequence is specified by a sequence of
       * additive address expressions with the same base for all
       * addresses in the chain.
       */
      } else if (is_additive_address_expr(addr_a0, NULL, &j) &&
                 is_additive_address_expr(addr_a1, addr_a0->src[1 - j].src.ssa, &k)) {
         if (i == 1)
            delta = nir_src_as_const_value(addr_a1->src[k].src)->u32 -
                    nir_src_as_const_value(addr_a0->src[j].src)->u32;
         else if (!is_additive_address_expr(addr_ai, addr_a0->src[1 - j].src.ssa, &l) ||
                  nir_src_as_const_value(addr_ai->src[l].src)->u32 != delta * i +
                     nir_src_as_const_value(addr_a0->src[j].src)->u32)
            return 0;

      } else {
         return 0;
      }
   }

   return delta;
}

/**
 * Hoist (move) an instruction \p def to a specified location in the
 * IR \p loc.  This moves an instruction earlier in the program to
 * ensure it's available when needed by the vectorized operation.
 *
 * Returns the moved definition, or NULL if movement isn't possible
 */
static struct nir_def *
hoist_def(struct nir_def *def, struct nir_def *loc)
{
   struct nir_instr *instr = nir_def_instr(def);
   if (nir_def_instr(loc)->block != instr->block ||
       instr->index <= nir_def_instr(loc)->index)
      return def;

   if (instr->type != nir_instr_type_load_const &&
       instr->type != nir_instr_type_alu)
      return NULL;

   if (instr->type == nir_instr_type_alu) {
      /* For ALU, recursively hoist the sources. */
      nir_alu_instr *alu = nir_instr_as_alu(instr);
      for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; i++)
         if (!hoist_def(alu->src[i].src.ssa, loc))
            return NULL;
   }

   nir_instr_move(nir_before_instr(nir_def_instr(loc)), instr);
   instr->index = nir_def_instr(loc)->index;

   return def;
}

/**
 * Helper function to find the location of insertion of the vectorized
 * version of the specified sequence of dot product chains.
 */
static nir_def *
find_sequence_location(const struct dp_entry *ent0, const struct dp_entry *ent1)
{
   nir_def *loc = NULL;

   /* Find the chain with the topmost \c last instruction. */
   for (const struct dp_entry *ent = ent0;;
        ent = list_entry(ent->head.next, struct dp_entry, head)) {
      if (!loc || ent->last->index < loc->index)
         loc = ent->last;

      if (ent == ent1)
         break;
   }

   return loc;
}

/**
 * Return whether there could be interfering memory operations in the
 * range of instructions between the topmost 'a' operand memory load
 * of dot product entry \ent0 and the insertion location \p loc1 where
 * those loads will be vectorized.
 *
 * Note that since the A memory loads of all dot product chains
 * vectorized into a single matrix multiplication are required to
 * match, we only need to check \p ent0 for memory interference.
 *
 * XXX - This implements the simplest possible check that is effective
 *       at dealing with transformations local to a single block, in
 *       the interest of compile-time efficiency, but it is
 *       unnecessarily strict.  Possibly extend into a global
 *       interference check, and possibly allow stores to a buffer
 *       that can be proven disjoint from the load's buffer, e.g. in
 *       case where the load has a "restrict" qualifier, if some
 *       use-case comes up that could benefit from that.
 */
static bool
has_interfering_memory_operations(const struct dp_entry *ent0, nir_def *loc1)
{
   nir_def *loc0 = NULL;

   for (unsigned i = 0; i < XEHP_SYSTOLIC_DEPTH; i++) {
      if (!loc0 || loc0->index > ent0->key.a[i]->index)
         loc0 = ent0->key.a[i];
   }

   nir_instr *inst0 = nir_def_instr(loc0);
   nir_instr *inst1 = nir_def_instr(loc1);

   if (inst0->block != inst1->block)
      return true;

   for (nir_instr *inst = inst0; inst != inst1; inst = nir_instr_next(inst)) {
      if (inst->type == nir_instr_type_intrinsic) {
         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(inst);
         if (intr->intrinsic == nir_intrinsic_store_ssbo ||
             intr->intrinsic == nir_intrinsic_store_ssbo_intel ||
             intr->intrinsic == nir_intrinsic_store_ssbo_block_intel ||
             (intr->intrinsic == nir_intrinsic_barrier &&
              (nir_intrinsic_memory_modes(intr) & nir_var_mem_ssbo)))
            return true;
      }
   }

   return false;
}

/**
 * Create and store a matrix defined from an \p n -component vector
 * \p rows.
 */
static nir_deref_instr *
build_matrix(nir_builder *b, nir_def *rows, unsigned n, const char *name)
{
   nir_variable *mat = nir_local_variable_create(
      b->impl, glsl_vector_type(GLSL_TYPE_INT, n), name);
   nir_deref_instr *deref = nir_build_deref_var(b, mat);
   nir_store_deref(b, deref, rows, nir_component_mask(n));
   return deref;
}

/**
 * Create and store the left (A) matrix operand.
 *
 * Since we require the one of the operands to be a convergent memory
 * load, we can vectorize it by converting it into a divergent memory
 * load where each lane calculates an offset that increases linearly
 * so as to give the effect of a transposed load without any
 * particular hardware support.
 *
 * Note that currently only SSBO loads are supported, though the same
 * approach can be made to work for UBOs or other kinds of memory
 * objects.
 */
static nir_deref_instr *
build_a_matrix(nir_builder *b, const struct dp_entry *ent0, nir_def *loc,
               unsigned n, const struct intel_device_info *devinfo)
{
   const nir_intrinsic_instr *load_a0 = nir_def_as_intrinsic(ent0->key.a[0]);
   nir_def *load_a0_addr = hoist_def(load_a0->src[1].ssa, loc);
   if (!load_a0_addr)
      return false;

   if (nir_def_instr(load_a0->src[0].ssa)->index > loc->index)
      return false;

   /* Calculate stride between sequential memory loads in the chain. */
   const unsigned delta = stride_of_a_linear_offsets(ent0, load_a0);
   if (!delta)
      return false;

   /* Calculate the address for the vectorized load starting with the
    * offset of the base component.
    */
   nir_def *base = nir_iadd(b, load_a0_addr,
                            nir_imm_int(b, nir_intrinsic_base(load_a0) +
                                        ent0->a_comp * 4));

   /* Calculate subgroup offset based on the subgroup ID.  Note that
    * because the A matrix is expected to have \c XEHP_SYSTOLIC_DEPTH
    * columns which is half the expected subgroup size on Xe2+, we can
    * fetch two rows per component of the vectorized load as long as
    * we're okay with getting the rows of the result permuted as below:
    *
    *   X@GRF0:  r_0 r_4
    *   Y@GRF1:  r_1 r_5
    *   Z@GRF2:  r_2 r_6
    *   W@GRF3:  r_3 r_7
    */
   assert(XEHP_SYSTOLIC_WIDTH(devinfo) == 2 * XEHP_SYSTOLIC_DEPTH);
   nir_def *subgroup_id = nir_load_subgroup_invocation(b);
   nir_def *subgroup_offset =
      nir_iadd(b, nir_imul(b, nir_iand(b, subgroup_id, nir_imm_int(b, 0x7)),
                           nir_imm_int(b, delta)),
               nir_imul(b, nir_ushr(b, subgroup_id, nir_imm_int(b, 3)),
                        nir_imm_int(b, XEHP_SYSTOLIC_CHANNEL_BITS / 8 * n / 2)));
   nir_def *addr = nir_iadd(b, base, subgroup_offset);

   /* Perform the vectorized SSBO load. */
   nir_def *load_a = nir_load_ssbo_intel(b, n / 2, XEHP_SYSTOLIC_CHANNEL_BITS,
                                         load_a0->src[0].ssa, addr,
                                         .access = nir_intrinsic_access(load_a0),
                                         .align_mul = nir_intrinsic_align_mul(load_a0),
                                         .align_offset = nir_intrinsic_align_offset(load_a0),
                                         .base = 0);

   return build_matrix(b, load_a, n / 2, "mat_a");
}

/**
 * Evaluate the permutation applied to the rows of the A matrix.
 *
 * Gives the index of logical row \p i of A matrix in the permuted
 * layout we use for computation, assuming the height of the A matrix
 * is given by \p n.
 *
 * We store the A matrix in this layout so that two 8-wide rows "r_i
 * r_(i+n/2)" can be fetched per 16-wide component of the emitted SSBO
 * load, improving the efficiency of the memory operations.  See
 * build_a_matrix() for additional details.
 */
static unsigned
permuted_a_matrix_row(unsigned n, unsigned i)
{
   return 2 * (i % (n / 2)) + (i >= n / 2);
}

/**
 * Create and store the right (B) matrix operand.
 *
 * No transformation is done to the rows beyond ensuring that they are
 * packed contiguously, since we set the type of the DPAS operands to
 * match the multiplicative 4x8 operands of the sdot intrinsic, and we
 * map the operations of individual channels of the sdot intrinsic
 * into the operations of individual channels of the DPAS
 * instructions, so the B matrix matches the layout of the original B
 * values precisely, beyond being concatenated as a matrix.  Arbitrary
 * divergent values are expected.
 */
static nir_deref_instr *
build_b_matrix(nir_builder *b, const struct dp_entry *ent0, nir_def *loc,
               unsigned m)
{
    nir_def *defs_b[NIR_MAX_VEC_COMPONENTS];
    for (unsigned i = 0; i < m; i++) {
       struct nir_def *def = hoist_def(ent0->key.b[i], loc);
       if (!def)
          return false;

       defs_b[i] = nir_channel(b, ent0->key.b[i], ent0->key.b_comp[i]);
    }

    return build_matrix(b, nir_vec(b, defs_b, m), m, "mat_b");
}

/**
 * Helper function to collect the accumulator (D) matrix operand.
 */
static nir_deref_instr *
build_acc_matrix(nir_builder *b, const struct dp_entry *ent0,
                 const struct dp_entry *ent1, nir_def *loc, unsigned n)
{
   nir_def *defs_d[NIR_MAX_VEC_COMPONENTS];
   unsigned i = 0;

   for (const struct dp_entry *ent = ent0;;
        ent = list_entry(ent->head.next, struct dp_entry, head)) {
      /* Get accumulator operand for the first dot product of each
       * chain.
       */
      nir_def *def = hoist_def(nir_def_as_alu(ent->first)->src[2].src.ssa,
                               loc);
      if (!def)
         return NULL;

      /* Permute the order of rows in the D matrix so it matches the
       * permutation applied to the rows of the A matrix (see
       * build_a_matrix() for the rationale).
       */
      defs_d[permuted_a_matrix_row(n, i)] = def;
      i++;

      if (ent == ent1)
         break;
   }

   return build_matrix(b, nir_vec(b, defs_d, n), n, "mat_d");
}

/**
 * Replace the uses of the results of the original dot products with
 * channels from the matrix product.
 */
static void
rewrite_scalar_results_with_matrix(nir_builder *b, const struct dp_entry *ent0,
                                   const struct dp_entry *ent1,
                                   nir_deref_instr *deref_e, unsigned n)
{
    unsigned i = 0;
    for (const struct dp_entry *ent = ent0;;
         ent = list_entry(ent->head.next, struct dp_entry, head)) {
       assert(i < n);
       /* The rows of the result are permuted according to the
        * transformation applied to the A and D matrices, see
        * build_a_matrix() for the rationale.
        */
       nir_def *chan = nir_channel(b, nir_load_deref(b, deref_e),
                                   permuted_a_matrix_row(n, i));

       /* Replace uses of the original scalar result. */
       nir_def_rewrite_uses_after_instr(ent->last, chan, nir_def_instr(ent->last));
       i++;

       if (ent == ent1)
          break;
    }
}

/**
 * Emit a DPAS (Dot Product Accumulate Systolic) instruction that
 * replaces a sequence of dot product additive chains with a single
 * matrix multiplication.
 *
 * The sequence of dot product chains vectorized into a matrix
 * multiplication is specified as the range in a linked list between
 * \p ent0 and \p ent1 inclusive.
 *
 * The left matrix operand is required to have dimensions \p n by \p
 * m, and the right matrix operand is required to have dimensions \p m
 * by XE2_SYSTOLIC_WIDTH.
 *
 * Returns true if vectorization was successful.
 */
static bool
emit_dpas(const struct intel_device_info *devinfo,
          const struct dp_entry *ent0, const struct dp_entry *ent1,
          unsigned n, unsigned m)
{
   /* Don't emit a matrix multiplication if we only have one dot
    * product chain.  Based on our current performance data, dpas.8x1
    * isn't really better than dp4a in terms of throughput, and doing
    * the transformation regardless will have some overhead as a
    * result of the transformation applied to the operands, so it
    * seems like we're better off ignoring any matrix multiplications
    * smaller than a dpas.8x2.
    *
    * XXX - Check if dpas.8x1 could be beneficial in cases where we
    *       could emit "Atomic" sequences where read supression of one
    *       of the sources is possible.
    */
   if (n == 1)
      return false;

   /* Find the correct location for the vectorized instruction. */
   nir_def *loc = find_sequence_location(ent0, ent1);
   if (has_interfering_memory_operations(ent0, loc))
      return false;

   nir_builder b = nir_builder_at(nir_after_instr(nir_def_instr(loc)));

   /* Collect accumulator matrix inputs. */
   nir_deref_instr *deref_d =
      build_acc_matrix(&b, ent0, ent1, loc, n);
   if (!deref_d)
      return false;

   /* Construct the A matrix. */
   nir_deref_instr *deref_a = build_a_matrix(&b, ent0, loc, n, devinfo);
   if (!deref_a)
      return false;

   /* Construct the B matrix. */
   nir_deref_instr *deref_b = build_b_matrix(&b, ent0, loc, m);
   if (!deref_b)
      return false;

   /* Emit the DPAS instruction that multiplies matrices A and B and
    * adds them to D.
    */
   nir_def *result = nir_dpas_intel(&b, XEHP_SYSTOLIC_CHANNEL_BITS,
                                    nir_load_deref(&b, deref_d),
                                    nir_load_deref(&b, deref_b),
                                    nir_load_deref(&b, deref_a),
                                    .dest_base_type = GLSL_TYPE_INT,
                                    .src_base_type = GLSL_TYPE_INT8,
                                    .saturate = false,
                                    .systolic_depth = m,
                                    .repeat_count = n);

   /* Store the result matrix. */
   nir_deref_instr *deref_e = build_matrix(&b, result, n, "mat_e");

   /* Replace the original scalar results with channels from the
    * matrix product.
    */
   rewrite_scalar_results_with_matrix(&b, ent0, ent1, deref_e, n);

   return true;
}

/**
 * Insert a dp_entry into the hash table, grouping into bins with
 * matching argument defs but different component of the A (left)
 * operands.
 */
static void
insert_dp_entry(void *ctx, struct hash_table *ht, struct dp_entry *ent)
{
   struct hash_entry *old_entry =
      _mesa_hash_table_search_pre_hashed(ht, hash_dp_key(&ent->key),
                                         &ent->key);
   if (old_entry) {
      struct list_head *list = old_entry->data;

      /* Insert in component order for efficient processing later. */
      list_for_each_entry(struct dp_entry, hnt, list, head) {
         if (hnt->a_comp > ent->a_comp) {
            list_addtail(&ent->head, &hnt->head);
            break;
         }
      }

      if (!list_is_linked(&ent->head))
         list_addtail(&ent->head, list);

   } else {
      struct list_head *list = ralloc(ctx, struct list_head);
      list_inithead(list);
      list_add(&ent->head, list);
      _mesa_hash_table_insert_pre_hashed(ht, hash_dp_key(&ent->key),
                                         &ent->key, list);
   }
}

/**
 * Verify that operands are suitable for vectorization.  If they are,
 * the index of the operand that can be used as B matrix (right
 * operand) is returned as the output parameter \p flip.
 */
static bool
verify_operands_suitable(nir_instr **instrs, unsigned *flip)
{
   for (*flip = 0; *flip < 2; (*flip)++) {
      const nir_alu_instr *alu0 = nir_instr_as_alu(instrs[0]);
      nir_intrinsic_instr *load_a0 = nir_def_as_intrinsic_or_null(
         alu0->src[1 - *flip].src.ssa);

      for (unsigned i = 0; i < XEHP_SYSTOLIC_DEPTH; i++) {
         const nir_alu_instr *alu = nir_instr_as_alu(instrs[i]);
         nir_intrinsic_instr *load_ai = nir_def_as_intrinsic_or_null(
            alu->src[1 - *flip].src.ssa);

         /* We require the A operands to be supported memory load
          * instructions with matching swizzle across the chain.
          */
         if (!load_ai || !is_supported_memory_load(load_ai) ||
             alu->src[1 - *flip].swizzle[0] != alu0->src[1 - *flip].swizzle[0])
            goto fail;

         /* The buffer is required to match across the chain. */
         if (load_ai->src[0].ssa != load_a0->src[0].ssa)
            goto fail;

         /* The load is required to be convergent for vectorization of
          * the load to be possible.
          */
         if (nir_src_is_divergent(&load_ai->src[1]))
            goto fail;

         /* The access mode is required to be matching across the chain. */
         if (nir_intrinsic_access(load_ai) != nir_intrinsic_access(load_a0))
            goto fail;
      }

      return true;
     fail:
      continue;
   }

   return false;
}

/**
 * Scan through all dot product instructions in the block identifying
 * suitable sequences of 8 chained dot products (the depth of the
 * systolic array as of XeHP), then enter them into the hash table
 * gathering chains that use different components of the same A vector
 * arguments into the same bin.
 */
static struct hash_table *
classify_dot_products_from_block(void *ctx, nir_block *block)
{
   struct hash_table *ht = _mesa_hash_table_create(ctx, hash_dp_key, dp_keys_equal);

   nir_foreach_instr(instr, block) {
      if (instr->type != nir_instr_type_alu)
         continue;

      /* Look for signed dot product with 4x8-bit operands.
       * XXX - Allow other dot product intrinsics.
       */
      nir_alu_instr *alu = nir_instr_as_alu(instr);
      if (alu->op != nir_op_sdot_4x8_iadd)
         continue;

      /* Track the depth of the additive chain of dot products that
       * ends at this instruction.
       */
      const nir_instr *accum_parent = nir_def_instr(alu->src[2].src.ssa);
      unsigned depth = accum_parent->pass_flags + 1;

      /* When we find a chain of XEHP_SYSTOLIC_DEPTH dot products
       * enter it into the hash table.
       */
      if (depth == XEHP_SYSTOLIC_DEPTH) {
         /* Backtrack through the chain to collect the chained
          * instructions for easier processing.
          */
         nir_instr *instrs[XEHP_SYSTOLIC_DEPTH];

         for (int i = XEHP_SYSTOLIC_DEPTH - 1; i >= 0; i--) {
            instrs[i] = (i == XEHP_SYSTOLIC_DEPTH - 1 ? instr :
                         nir_def_instr(nir_instr_as_alu(
                                          instrs[i + 1])->src[2].src.ssa));
         }

         /* Verify that operands are suitable for vectorization. */
         unsigned flip = 0;
         if (!verify_operands_suitable(instrs, &flip))
            continue;

         /* Set up a new entry for the vectorization candidate. */
         struct dp_entry *ent = rzalloc(ctx, struct dp_entry);
         ent->a_comp = nir_instr_as_alu(instrs[0])->src[1 - flip].swizzle[0];
         ent->first = nir_instr_def(instrs[0]);
         ent->last = nir_instr_def(instrs[7]);

         /* Set up the candidate's key from the instruction operands. */
         for (unsigned i = 0; i < ARRAY_SIZE(instrs); i++) {
            ent->key.a[i] = nir_instr_as_alu(instrs[i])->src[1 - flip].src.ssa;
            ent->key.b[i] = nir_instr_as_alu(instrs[i])->src[flip].src.ssa;
            ent->key.b_comp[i] = nir_instr_as_alu(instrs[i])->src[flip].swizzle[0];
         }

         /* Insert the entry into the hash table. */
         insert_dp_entry(ctx, ht, ent);

         /* Reset depth after processing. */
         depth = 0;
      }

      instr->pass_flags = depth;
   }

   return ht;
}

/**
 * Scan through each bin in the dot product hash table local to a
 * block, identifying sequences of chains that use consecutive
 * components of the same A vector which can be promoted into a matrix
 * multiplication.
 */
static bool
vectorize_sequential_dot_products(const struct intel_device_info *devinfo,
                                  struct hash_table *ht)
{
   bool progress = false;

   hash_table_foreach(ht, entry) {
      const struct list_head *list = entry->data;
      const struct dp_entry *ent0 = NULL;
      unsigned i = 0;

      list_for_each_entry(struct dp_entry, ent1, list, head) {
         if (!ent0) {
            /* Start a new sequence */
            ent0 = ent1;
            i = 1;
         } else if (ent1->a_comp == ent0->a_comp + i && i < XEHP_SYSTOLIC_REPS) {
            /* Extend the sequence if components are consecutive. */
            i++;
         } else {
            /* Apply vectorization optimization to previous sequence. */
            if (emit_dpas(devinfo, ent0,
                          list_entry(ent1->head.prev, struct dp_entry, head),
                          i, XEHP_SYSTOLIC_DEPTH))
               progress = true;

            /* Start a new sequence */
            ent0 = ent1;
            i = 1;
         }
      }

      assert(ent0);

      /* Apply vectorization optimization to the final sequence. */
      if (emit_dpas(devinfo, ent0,
                    list_last_entry(list, struct dp_entry, head),
                    i, XEHP_SYSTOLIC_DEPTH))
         progress = true;
   }

   return progress;
}

/**
 * Optimization pass to convert sequences of dot product operations
 * into systolic array operations (DPAS instructions) for XeHP+ GPUs.
 *
 * A matrix multiplication as performed by DPAS can be expressed like:
 *
 *   E^i_k = D^i_k + Sum_j A^i_j B^j_k
 *
 * IOW each scalar component of a matrix multiplication is just a
 * (possibly large) dot product.  This pass identifies such chains of
 * 4x8 integer dot products in the program and bins them according to
 * the A and B arguments used.  Sequences of dot products with
 * consecutive components are transformed into a matrix product for
 * each densely occupied interval of indices within each bin, as long
 * as there is an efficient way to transpose one of the arguments
 * (which will be denoted A matrix) in the register file.
 *
 * Currently the transpose is done by requiring that one of the
 * arguments (A^i_j) are loaded from memory from a convergent address
 * that increases linearly with the i and j indices, so a transpose
 * matrix can be obtained in a form that can be consumed by the DPAS
 * instruction by vectorizing the set of convergent loads into a
 * single divergent load that specifies an address that increases
 * linearly with the subgroup index.
 *
 * In cases where one of the arguments of the dot products satisfies
 * that condition, the other (B) argument can be pretty much
 * unrestricted, arbitrary divergent values are vectorized and can be
 * consumed as B argument of the DPAS sequence.
 */
bool
brw_nir_opt_systolic_vectorize(nir_shader *shader,
                               const struct intel_device_info *devinfo)
{
   if (devinfo->ver < 20 ||
       shader->info.min_subgroup_size > XEHP_SYSTOLIC_WIDTH(devinfo) ||
       shader->info.max_subgroup_size < XEHP_SYSTOLIC_WIDTH(devinfo)) {
      /* XXX - Enable pre-Xe2. */
      return false;
   }

   bool progress = false;

   nir_divergence_analysis(shader);

   nir_shader_clear_pass_flags(shader);

   nir_foreach_function_impl(impl, shader) {
      bool progress_impl = false;

      nir_metadata_require(impl, nir_metadata_instr_index);

      nir_foreach_block(block, impl) {
         /* XXX - The matrix multiplication arithmetic emitted by this
          *       works correctly under divergent control flow,
          *       however the memory loads we emit to set up the A
          *       matrix require the whole subgroup to be active, so
          *       this currently doesn't work, but it could be made to
          *       work if a use-case is found by having the back-end
          *       expose an additional intrinsic to do the same thing
          *       with a NoMask memory load.
          */
         if (block->divergent)
            continue;

         void *blk_ctx = ralloc_context(NULL);

         /* Bin chains of dot products with matching A and B arguments
          * up to a swizzle of the A vector.
          */
         struct hash_table *ht = classify_dot_products_from_block(blk_ctx, block);

         /* Identify sequences of chains from the same bin with
          * consecutive A components and vectorize them into a matrix
          * multiplication.
          */
         progress_impl |= vectorize_sequential_dot_products(devinfo, ht);

         ralloc_free(blk_ctx);
      }

      progress |= nir_progress(progress_impl, impl, 0);
   }

   if (progress) {
      /* Since currently the backend only exposes a DPAS intrinsic of
       * fixed width equal to XEHP_SYSTOLIC_WIDTH we have to limit the
       * subgroup size of the shader to the width of the systolic
       * pipeline, so that the DPAS instructions emitted above the
       * same execution width as the (subgroup-wide) dot product
       * instructions they replace.
       */
      shader->info.min_subgroup_size =
         shader->info.max_subgroup_size = XEHP_SYSTOLIC_WIDTH(devinfo);
   }

   return progress;
}
