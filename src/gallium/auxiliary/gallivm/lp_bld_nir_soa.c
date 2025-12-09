/**************************************************************************
 *
 * Copyright 2019 Red Hat.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **************************************************************************/

#include "lp_bld_nir.h"
#include "lp_bld_init.h"
#include "lp_bld_conv.h"
#include "lp_bld_debug.h"
#include "lp_bld_flow.h"
#include "lp_bld_logic.h"
#include "lp_bld_gather.h"
#include "lp_bld_const.h"
#include "lp_bld_quad.h"
#include "lp_bld_struct.h"
#include "lp_bld_jit_types.h"
#include "lp_bld_arit.h"
#include "lp_bld_bitarit.h"
#include "lp_bld_coro.h"
#include "lp_bld_printf.h"
#include "lp_bld_intr.h"
#include "lp_bld_tgsi.h"

#include "util/u_cpu_detect.h"
#include "util/u_math.h"

#include "nir_deref.h"
#include "nir_search_helpers.h"

static bool
lp_nir_instr_src_divergent(nir_instr *instr, uint32_t src_index)
{
   switch (instr->type) {
   case nir_instr_type_alu: {
      nir_alu_instr *alu = nir_instr_as_alu(instr);
      return alu->def.divergent;
   }
   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
      switch (intr->intrinsic) {
      /* Instructions which always take uniform sources */
      case nir_intrinsic_load_const_buf_base_addr_lvp:
      case nir_intrinsic_set_vertex_and_primitive_count:
      case nir_intrinsic_launch_mesh_workgroups:
         return false;

      /* Instructions which always take divergent sources */
      case nir_intrinsic_ssbo_atomic:
      case nir_intrinsic_ssbo_atomic_swap:
      case nir_intrinsic_shared_atomic:
      case nir_intrinsic_shared_atomic_swap:
      case nir_intrinsic_global_atomic:
      case nir_intrinsic_global_atomic_swap:
      case nir_intrinsic_task_payload_atomic:
      case nir_intrinsic_task_payload_atomic_swap:
      case nir_intrinsic_store_global:
      case nir_intrinsic_load_scratch:
      case nir_intrinsic_store_scratch:
      case nir_intrinsic_store_deref:
      case nir_intrinsic_store_shared:
      case nir_intrinsic_store_task_payload:
      case nir_intrinsic_demote_if:
      case nir_intrinsic_ballot:
      case nir_intrinsic_vote_all:
      case nir_intrinsic_vote_any:
      case nir_intrinsic_vote_ieq:
      case nir_intrinsic_vote_feq:
      case nir_intrinsic_interp_deref_at_offset:
      case nir_intrinsic_interp_deref_at_sample:
      case nir_intrinsic_ddx:
      case nir_intrinsic_ddy:
      case nir_intrinsic_ddx_coarse:
      case nir_intrinsic_ddy_coarse:
      case nir_intrinsic_ddx_fine:
      case nir_intrinsic_ddy_fine:
      case nir_intrinsic_load_reg_indirect:
      case nir_intrinsic_store_reg:
      case nir_intrinsic_store_reg_indirect:
      case nir_intrinsic_reduce:
      case nir_intrinsic_inclusive_scan:
      case nir_intrinsic_exclusive_scan:
      case nir_intrinsic_shuffle:
         return true;

      case nir_intrinsic_image_load:
      case nir_intrinsic_bindless_image_load:
      case nir_intrinsic_bindless_image_sparse_load:
      case nir_intrinsic_image_store:
      case nir_intrinsic_bindless_image_store:
      case nir_intrinsic_image_atomic:
      case nir_intrinsic_image_atomic_swap:
      case nir_intrinsic_bindless_image_atomic:
      case nir_intrinsic_bindless_image_atomic_swap:
         return src_index != 0;

      case nir_intrinsic_store_ssbo:
         /* The data source should be divergent if the descriptor/offset are divergent.
          * The offset source should be divergent if the descriptor is divergent.
          */
         if (src_index == 0 || src_index == 2)
            return nir_src_is_divergent(&intr->src[1]) || nir_src_is_divergent(&intr->src[2]);
         return nir_src_is_divergent(&intr->src[src_index]);

      case nir_intrinsic_load_ssbo:
         /* The offset source should be divergent if the descriptor is divergent. */
         if (src_index == 1)
            return nir_src_is_divergent(&intr->src[0]) || nir_src_is_divergent(&intr->src[1]);
         return nir_src_is_divergent(&intr->src[src_index]);

      case nir_intrinsic_load_ubo:
         return src_index == 0 ? false : nir_src_is_divergent(&intr->src[src_index]);

      default:
         return nir_src_is_divergent(&intr->src[src_index]);
      }
   }
   case nir_instr_type_tex: {
      nir_tex_instr *tex = nir_instr_as_tex(instr);
      switch (tex->src[src_index].src_type) {
      case nir_tex_src_texture_handle:
      case nir_tex_src_sampler_handle:
         return false;
      default:
         return true;
      }
   }
   case nir_instr_type_deref: {
      nir_deref_instr *deref = nir_instr_as_deref(instr);
      /* Shader IO handling assumes that array indices are divergent. */
      return src_index == 0 ? nir_src_is_divergent(&deref->parent) : true;
   }
   case nir_instr_type_call:
      return true;
   default:
      UNREACHABLE("Unhandled instruction type");
   }
}

struct lp_build_nir_soa_context
{
   struct lp_build_context base;
   struct lp_build_context uint_bld;
   struct lp_build_context int_bld;
   struct lp_build_context uint8_bld;
   struct lp_build_context int8_bld;
   struct lp_build_context uint16_bld;
   struct lp_build_context int16_bld;
   struct lp_build_context half_bld;
   struct lp_build_context dbl_bld;
   struct lp_build_context uint64_bld;
   struct lp_build_context int64_bld;
   struct lp_build_context bool_bld;

   struct lp_build_context scalar_base;
   struct lp_build_context scalar_uint_bld;
   struct lp_build_context scalar_int_bld;
   struct lp_build_context scalar_uint8_bld;
   struct lp_build_context scalar_int8_bld;
   struct lp_build_context scalar_uint16_bld;
   struct lp_build_context scalar_int16_bld;
   struct lp_build_context scalar_half_bld;
   struct lp_build_context scalar_dbl_bld;
   struct lp_build_context scalar_uint64_bld;
   struct lp_build_context scalar_int64_bld;
   struct lp_build_context scalar_bool_bld;

   LLVMValueRef *ssa_defs;
   struct hash_table *regs;
   struct hash_table *vars;
   struct hash_table *fns;

   /** Value range analysis hash table used in code generation. */
   struct hash_table *range_ht;

   LLVMValueRef func;
   nir_shader *shader;
   nir_instr *instr;

   /* A variable that contains the value of mask_vec so it shows up inside the debugger. */
   LLVMValueRef debug_exec_mask;

   struct lp_build_if_state if_stack[LP_MAX_TGSI_NESTING];
   uint32_t if_stack_size;

   LLVMValueRef consts_ptr;
   const LLVMValueRef (*inputs)[TGSI_NUM_CHANNELS];
   LLVMValueRef (*outputs)[TGSI_NUM_CHANNELS];
   int num_inputs;
   LLVMTypeRef context_type;
   LLVMValueRef context_ptr;
   LLVMTypeRef resources_type;
   LLVMValueRef resources_ptr;
   LLVMTypeRef thread_data_type;
   LLVMValueRef thread_data_ptr;
   LLVMValueRef null_qword_ptr;
   LLVMValueRef noop_store_ptr;

   LLVMValueRef ssbo_ptr;

   LLVMValueRef shared_ptr;
   LLVMValueRef shared_size;
   LLVMValueRef payload_ptr;
   LLVMValueRef scratch_ptr;
   unsigned scratch_size;

   const struct lp_build_coro_suspend_info *coro;

   const struct lp_build_sampler_soa *sampler;
   const struct lp_build_image_soa *image;

   const struct lp_build_gs_iface *gs_iface;
   const struct lp_build_tcs_iface *tcs_iface;
   const struct lp_build_tes_iface *tes_iface;
   const struct lp_build_fs_iface *fs_iface;
   const struct lp_build_mesh_iface *mesh_iface;

   LLVMValueRef emitted_prims_vec_ptr[PIPE_MAX_VERTEX_STREAMS];
   LLVMValueRef total_emitted_vertices_vec_ptr[PIPE_MAX_VERTEX_STREAMS];
   LLVMValueRef emitted_vertices_vec_ptr[PIPE_MAX_VERTEX_STREAMS];
   LLVMValueRef max_output_vertices_vec;
   struct lp_bld_tgsi_system_values system_values;

   nir_variable_mode indirects;
   struct lp_build_mask_context *mask;
   struct lp_exec_mask exec_mask;

   /* We allocate/use this array of inputs if (indirects & nir_var_shader_in) is
    * set. The inputs[] array above is unused then.
    */
   LLVMValueRef inputs_array;

   unsigned gs_vertex_streams;

   LLVMTypeRef call_context_type;
   LLVMValueRef call_context_ptr;
};

static inline struct lp_build_context *
get_flt_bld(struct lp_build_nir_soa_context *bld,
            unsigned op_bit_size, bool divergent)
{
   switch (op_bit_size) {
   case 64:
      return divergent ? &bld->dbl_bld : &bld->scalar_dbl_bld;
   case 16:
      return divergent ? &bld->half_bld : &bld->scalar_half_bld;
   default:
   case 32:
      return divergent ? &bld->base : &bld->scalar_base;
   }
}

static inline struct lp_build_context *
get_int_bld(struct lp_build_nir_soa_context *bld,
            bool is_unsigned,
            unsigned op_bit_size,
            bool divergent)
{
   if (is_unsigned) {
      switch (op_bit_size) {
      case 64:
         return divergent ? &bld->uint64_bld : &bld->scalar_uint64_bld;
      case 32:
      default:
         return divergent ? &bld->uint_bld : &bld->scalar_uint_bld;
      case 16:
         return divergent ? &bld->uint16_bld : &bld->scalar_uint16_bld;
      case 8:
         return divergent ? &bld->uint8_bld : &bld->scalar_uint8_bld;
      case 1:
         return divergent ? &bld->bool_bld : &bld->scalar_bool_bld;
      }
   } else {
      switch (op_bit_size) {
      case 64:
         return divergent ? &bld->int64_bld : &bld->scalar_int64_bld;
      default:
      case 32:
         return divergent ? &bld->int_bld : &bld->scalar_int_bld;
      case 16:
         return divergent ? &bld->int16_bld : &bld->scalar_int16_bld;
      case 8:
         return divergent ? &bld->int8_bld : &bld->scalar_int8_bld;
      case 1:
         return divergent ? &bld->bool_bld : &bld->scalar_bool_bld;
      }
   }
}

static int bit_size_to_shift_size(int bit_size)
{
   switch (bit_size) {
   case 64:
      return 3;
   default:
   case 32:
      return 2;
   case 16:
      return 1;
   case 8:
      return 0;
   }
}

/*
 * Combine the global mask if there is one with the current execution mask.
 */
static LLVMValueRef
mask_vec(struct lp_build_nir_soa_context *bld)
{
   struct lp_exec_mask *exec_mask = &bld->exec_mask;
   LLVMValueRef bld_mask = bld->mask ? lp_build_mask_value(bld->mask) : NULL;
   if (!exec_mask->has_mask)
      return bld_mask;
   if (!bld_mask)
      return exec_mask->exec_mask;

   LLVMBuilderRef builder = bld->base.gallivm->builder;
   return LLVMBuildAnd(builder, lp_build_mask_value(bld->mask),
                       exec_mask->exec_mask, "");
}

/*
 * Use the execution mask if there is one, otherwise don't mask (ignore global mask).
 * This allows helper invocations to run, which are necessary for correct derivatives.
 */
static LLVMValueRef
mask_vec_with_helpers(struct lp_build_nir_soa_context *bld)
{
   if (bld->shader->info.stage != MESA_SHADER_FRAGMENT)
      return mask_vec(bld); /* No helper invocations needed. */

   struct lp_exec_mask *exec_mask = &bld->exec_mask;
   if (exec_mask->has_mask)
      return exec_mask->exec_mask;

   return lp_build_const_int_vec(bld->base.gallivm,
                                 bld->uint_bld.type, -1);
}

static LLVMValueRef
group_op_mask_vec(struct lp_build_nir_soa_context *bld)
{
   if (bld->shader->info.fs.require_full_quads)
      return mask_vec_with_helpers(bld);

   return mask_vec(bld);
}

static bool
lp_exec_mask_is_nz(struct lp_build_nir_soa_context *bld)
{
   if (bld->shader->info.stage == MESA_SHADER_FRAGMENT && bld->shader->info.fs.uses_discard)
      return false;

   return !bld->exec_mask.has_mask;
}

static bool
invocation_0_must_be_active(struct lp_build_nir_soa_context *bld)
{
   /* Fragment shaders may dispatch with invocation 0 inactive.  All other
    * stages have invocation 0 active at the top.  (See
    * lp_build_tgsi_params.mask setup in draw_llvm.c and lp_state_*.c)
    */
   if (bld->shader->info.stage == MESA_SHADER_FRAGMENT)
      return false;

   /* If we're in some control flow right now, then invocation 0 may be
    * disabled.
    */
   if (bld->exec_mask.has_mask)
      return false;

   return true;
}

/**
 * Returns a scalar value of the first active invocation in the exec_mask.
 *
 * Note that gallivm doesn't generally jump when exec_mask is 0 (such as if/else
 * branches thare are all false, or portions of a loop after a break/continue
 * has ended the last invocation that had been active in the loop).  In that
 * case, we return a 0 value so that unconditional LLVMBuildExtractElement of
 * the first_active_invocation (such as in memory loads, texture unit index
 * lookups, etc) will use a valid index
 */
static LLVMValueRef first_active_invocation(struct lp_build_nir_soa_context *bld, bool helpers)
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;
   struct lp_build_context *uint_bld = &bld->uint_bld;

   if (invocation_0_must_be_active(bld))
      return lp_build_const_int32(gallivm, 0);

   LLVMValueRef exec_mask = helpers ? mask_vec_with_helpers(bld) : mask_vec(bld);

   LLVMValueRef bitmask = LLVMBuildICmp(builder, LLVMIntNE, exec_mask, bld->uint_bld.zero, "exec_bitvec");
   /* Turn it from N x i1 to iN, then extend it up to i32 so we can use a single
    * cttz intrinsic -- I assume the compiler will drop the extend if there are
    * smaller instructions available, since we have is_zero_poison.
    */
   bitmask = LLVMBuildBitCast(builder, bitmask, LLVMIntTypeInContext(gallivm->context, uint_bld->type.length), "exec_bitmask");
   bitmask = LLVMBuildZExt(builder, bitmask, bld->int_bld.elem_type, "");

   LLVMValueRef any_active = LLVMBuildICmp(builder, LLVMIntNE, bitmask, lp_build_const_int32(gallivm, 0), "any_active");

   LLVMValueRef first_active = lp_build_intrinsic_binary(builder, "llvm.cttz.i32", bld->int_bld.elem_type, bitmask,
                                                         LLVMConstInt(LLVMInt1TypeInContext(gallivm->context), false, false));

   return LLVMBuildSelect(builder, any_active, first_active, lp_build_const_int32(gallivm, 0), "first_active_or_0");
}

static LLVMValueRef
lp_build_zero_bits(struct gallivm_state *gallivm, int bit_size, bool is_float)
{
   if (bit_size == 64)
      return LLVMConstInt(LLVMInt64TypeInContext(gallivm->context), 0, 0);
   else if (bit_size == 16)
      return LLVMConstInt(LLVMInt16TypeInContext(gallivm->context), 0, 0);
   else if (bit_size == 8)
      return LLVMConstInt(LLVMInt8TypeInContext(gallivm->context), 0, 0);
   else
      return is_float ? lp_build_const_float(gallivm, 0) : lp_build_const_int32(gallivm, 0);
}

static LLVMValueRef
emit_fetch_64bit(
   struct lp_build_nir_soa_context *bld,
   LLVMValueRef input,
   LLVMValueRef input2)
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;
   LLVMValueRef res;
   int i;
   LLVMValueRef shuffles[2 * (LP_MAX_VECTOR_WIDTH/32)];
   int len = bld->base.type.length * 2;
   assert(len <= (2 * (LP_MAX_VECTOR_WIDTH/32)));

   for (i = 0; i < bld->base.type.length * 2; i+=2) {
#if UTIL_ARCH_LITTLE_ENDIAN
      shuffles[i] = lp_build_const_int32(gallivm, i / 2);
      shuffles[i + 1] = lp_build_const_int32(gallivm, i / 2 + bld->base.type.length);
#else
      shuffles[i] = lp_build_const_int32(gallivm, i / 2 + bld->base.type.length);
      shuffles[i + 1] = lp_build_const_int32(gallivm, i / 2);
#endif
   }
   res = LLVMBuildShuffleVector(builder, input, input2, LLVMConstVector(shuffles, len), "");

   return LLVMBuildBitCast(builder, res, bld->dbl_bld.vec_type, "");
}

static void
emit_store_64bit_split(struct lp_build_nir_soa_context *bld,
                       LLVMValueRef value,
                       LLVMValueRef split_values[2])
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;
   unsigned i;
   LLVMValueRef shuffles[LP_MAX_VECTOR_WIDTH/32];
   LLVMValueRef shuffles2[LP_MAX_VECTOR_WIDTH/32];
   int len = bld->base.type.length * 2;

   value = LLVMBuildBitCast(gallivm->builder, value, LLVMVectorType(LLVMFloatTypeInContext(gallivm->context), len), "");
   for (i = 0; i < bld->base.type.length; i++) {
#if UTIL_ARCH_LITTLE_ENDIAN
      shuffles[i] = lp_build_const_int32(gallivm, i * 2);
      shuffles2[i] = lp_build_const_int32(gallivm, (i * 2) + 1);
#else
      shuffles[i] = lp_build_const_int32(gallivm, i * 2 + 1);
      shuffles2[i] = lp_build_const_int32(gallivm, i * 2);
#endif
   }

   split_values[0] = LLVMBuildShuffleVector(builder, value,
                                 LLVMGetUndef(LLVMTypeOf(value)),
                                 LLVMConstVector(shuffles,
                                                 bld->base.type.length),
                                 "");
   split_values[1] = LLVMBuildShuffleVector(builder, value,
                                  LLVMGetUndef(LLVMTypeOf(value)),
                                  LLVMConstVector(shuffles2,
                                                  bld->base.type.length),
                                  "");
}

static void
emit_store_64bit_chan(struct lp_build_nir_soa_context *bld,
                      LLVMValueRef chan_ptr,
                      LLVMValueRef chan_ptr2,
                      LLVMValueRef value)
{
   struct lp_build_context *float_bld = &bld->base;
   LLVMValueRef split_vals[2];

   emit_store_64bit_split(bld, value, split_vals);

   lp_exec_mask_store(&bld->exec_mask, float_bld, split_vals[0], chan_ptr);
   lp_exec_mask_store(&bld->exec_mask, float_bld, split_vals[1], chan_ptr2);
}

static LLVMValueRef
get_soa_array_offsets(struct lp_build_context *uint_bld,
                      LLVMValueRef indirect_index,
                      int num_components,
                      unsigned chan_index,
                      bool need_perelement_offset)
{
   struct gallivm_state *gallivm = uint_bld->gallivm;
   LLVMValueRef chan_vec =
      lp_build_const_int_vec(uint_bld->gallivm, uint_bld->type, chan_index);
   LLVMValueRef length_vec =
      lp_build_const_int_vec(gallivm, uint_bld->type, uint_bld->type.length);
   LLVMValueRef index_vec;

   /* index_vec = (indirect_index * num_components + chan_index) * length + offsets */
   index_vec = lp_build_mul(uint_bld, indirect_index, lp_build_const_int_vec(uint_bld->gallivm, uint_bld->type, num_components));
   index_vec = lp_build_add(uint_bld, index_vec, chan_vec);
   index_vec = lp_build_mul(uint_bld, index_vec, length_vec);

   if (need_perelement_offset) {
      LLVMValueRef pixel_offsets;
      unsigned i;
     /* build pixel offset vector: {0, 1, 2, 3, ...} */
      pixel_offsets = uint_bld->undef;
      for (i = 0; i < uint_bld->type.length; i++) {
         LLVMValueRef ii = lp_build_const_int32(gallivm, i);
         pixel_offsets = LLVMBuildInsertElement(gallivm->builder, pixel_offsets,
                                                ii, ii, "");
      }
      index_vec = lp_build_add(uint_bld, index_vec, pixel_offsets);
   }
   return index_vec;
}

static LLVMValueRef
build_gather(struct lp_build_nir_soa_context *bld,
             struct lp_build_context *gather_bld,
             LLVMTypeRef base_type,
             LLVMValueRef base_ptr,
             LLVMValueRef indexes,
             LLVMValueRef overflow_mask,
             LLVMValueRef indexes2)
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;
   struct lp_build_context *uint_bld = &bld->uint_bld;
   LLVMValueRef res;
   unsigned i;

   if (indexes2)
      res = LLVMGetUndef(LLVMVectorType(LLVMFloatTypeInContext(gallivm->context), bld->base.type.length * 2));
   else
      res = gather_bld->undef;
   /*
    * overflow_mask is a vector telling us which channels
    * in the vector overflowed. We use the overflow behavior for
    * constant buffers which is defined as:
    * Out of bounds access to constant buffer returns 0 in all
    * components. Out of bounds behavior is always with respect
    * to the size of the buffer bound at that slot.
    */

   if (overflow_mask) {
      /*
       * We avoid per-element control flow here (also due to llvm going crazy,
       * though I suspect it's better anyway since overflow is likely rare).
       * Note that since we still fetch from buffers even if num_elements was
       * zero (in this case we'll fetch from index zero) the jit func callers
       * MUST provide valid fake constant buffers of size 4x32 (the values do
       * not matter), otherwise we'd still need (not per element though)
       * control flow.
       */
      indexes = lp_build_select(uint_bld, overflow_mask, uint_bld->zero, indexes);
      if (indexes2)
         indexes2 = lp_build_select(uint_bld, overflow_mask, uint_bld->zero, indexes2);
   }

   /*
    * Loop over elements of index_vec, load scalar value, insert it into 'res'.
    */
   for (i = 0; i < gather_bld->type.length * (indexes2 ? 2 : 1); i++) {
      LLVMValueRef si, di;
      LLVMValueRef index;
      LLVMValueRef scalar_ptr, scalar;

      di = lp_build_const_int32(gallivm, i);
      if (indexes2)
         si = lp_build_const_int32(gallivm, i >> 1);
      else
         si = di;

      if (indexes2 && (i & 1)) {
         index = LLVMBuildExtractElement(builder,
                                         indexes2, si, "");
      } else {
         index = LLVMBuildExtractElement(builder,
                                         indexes, si, "");
      }

      scalar_ptr = LLVMBuildGEP2(builder, base_type, base_ptr, &index, 1, "gather_ptr");
      scalar = LLVMBuildLoad2(builder, base_type, scalar_ptr, "");

      res = LLVMBuildInsertElement(builder, res, scalar, di, "");
   }

   if (overflow_mask) {
      if (indexes2) {
         res = LLVMBuildBitCast(builder, res, bld->dbl_bld.vec_type, "");
         overflow_mask = LLVMBuildSExt(builder, overflow_mask,
                                       bld->dbl_bld.int_vec_type, "");
         res = lp_build_select(&bld->dbl_bld, overflow_mask,
                               bld->dbl_bld.zero, res);
      } else
         res = lp_build_select(gather_bld, overflow_mask, gather_bld->zero, res);
   }

   return res;
}

/**
 * Scatter/store vector.
 */
static void
emit_mask_scatter(struct lp_build_nir_soa_context *bld,
                  LLVMValueRef base_ptr,
                  LLVMValueRef indexes,
                  LLVMValueRef values,
                  struct lp_exec_mask *mask)
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;
   unsigned i;
   LLVMValueRef pred = mask->has_mask ? mask->exec_mask : NULL;

   /*
    * Loop over elements of index_vec, store scalar value.
    */
   for (i = 0; i < bld->base.type.length; i++) {
      LLVMValueRef ii = lp_build_const_int32(gallivm, i);
      LLVMValueRef index = LLVMBuildExtractElement(builder, indexes, ii, "");
      LLVMValueRef val = LLVMBuildExtractElement(builder, values, ii, "scatter_val");
      LLVMValueRef scalar_ptr = LLVMBuildGEP2(builder, LLVMTypeOf(val), base_ptr, &index, 1, "scatter_ptr");
      LLVMValueRef scalar_pred = pred ?
         LLVMBuildExtractElement(builder, pred, ii, "scatter_pred") : NULL;

      if (0)
         lp_build_printf(gallivm, "scatter %d: val %f at %d %p\n",
                         ii, val, index, scalar_ptr);

      if (scalar_pred) {
         LLVMValueRef real_val, dst_val;
         dst_val = LLVMBuildLoad2(builder, LLVMTypeOf(val), scalar_ptr, "");
         scalar_pred = LLVMBuildTrunc(builder, scalar_pred, LLVMInt1TypeInContext(gallivm->context), "");
         real_val = LLVMBuildSelect(builder, scalar_pred, val, dst_val, "");
         LLVMBuildStore(builder, real_val, scalar_ptr);
      }
      else {
         LLVMBuildStore(builder, val, scalar_ptr);
      }
   }
}

static void emit_load_var(struct lp_build_nir_soa_context *bld,
                           nir_variable_mode deref_mode,
                           unsigned num_components,
                           unsigned bit_size,
                           nir_variable *var,
                           unsigned vertex_index,
                           LLVMValueRef indir_vertex_index,
                           unsigned const_index,
                           LLVMValueRef indir_index,
                           LLVMValueRef result[NIR_MAX_VEC_COMPONENTS])
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   int dmul = bit_size == 64 ? 2 : 1;
   unsigned location = var->data.driver_location;
   unsigned location_frac = var->data.location_frac;

   if (!var->data.compact && !indir_index)
      location += const_index;
   else if (var->data.compact) {
      location += const_index / 4;
      location_frac += const_index % 4;
      const_index = 0;
   }
   switch (deref_mode) {
   case nir_var_shader_in:
      for (unsigned i = 0; i < num_components; i++) {
         int idx = (i * dmul) + location_frac;
         int comp_loc = location;

         if (bit_size == 64 && idx >= 4) {
            comp_loc++;
            idx = idx % 4;
         }

         if (bld->gs_iface) {
            LLVMValueRef vertex_index_val = lp_build_const_int32(gallivm, vertex_index);
            LLVMValueRef attrib_index_val = lp_build_const_int32(gallivm, comp_loc);
            LLVMValueRef swizzle_index_val = lp_build_const_int32(gallivm, idx);
            LLVMValueRef result2;

            result[i] = bld->gs_iface->fetch_input(bld->gs_iface, &bld->base,
                                                   false, vertex_index_val, 0, attrib_index_val, swizzle_index_val);
            if (bit_size == 64) {
               LLVMValueRef swizzle_index_val = lp_build_const_int32(gallivm, idx + 1);
               result2 = bld->gs_iface->fetch_input(bld->gs_iface, &bld->base,
                                                    false, vertex_index_val, 0, attrib_index_val, swizzle_index_val);
               result[i] = emit_fetch_64bit(bld, result[i], result2);
            }
         } else if (bld->tes_iface) {
            LLVMValueRef vertex_index_val = lp_build_const_int32(gallivm, vertex_index);
            LLVMValueRef attrib_index_val;
            LLVMValueRef swizzle_index_val = lp_build_const_int32(gallivm, idx);
            LLVMValueRef result2;

            if (indir_index) {
               if (var->data.compact) {
                  swizzle_index_val = lp_build_add(&bld->uint_bld, indir_index, lp_build_const_int_vec(gallivm, bld->uint_bld.type, idx));
                  attrib_index_val = lp_build_const_int32(gallivm, comp_loc);
               } else
                  attrib_index_val = lp_build_add(&bld->uint_bld, indir_index, lp_build_const_int_vec(gallivm, bld->uint_bld.type, comp_loc));
            } else
               attrib_index_val = lp_build_const_int32(gallivm, comp_loc);

            if (var->data.patch) {
               result[i] = bld->tes_iface->fetch_patch_input(bld->tes_iface, &bld->base,
                                                             indir_index ? true : false, attrib_index_val, swizzle_index_val);
               if (bit_size == 64) {
                  LLVMValueRef swizzle_index_val = lp_build_const_int32(gallivm, idx + 1);
                  result2 = bld->tes_iface->fetch_patch_input(bld->tes_iface, &bld->base,
                                                              indir_index ? true : false, attrib_index_val, swizzle_index_val);
                  result[i] = emit_fetch_64bit(bld, result[i], result2);
               }
            }
            else {
               result[i] = bld->tes_iface->fetch_vertex_input(bld->tes_iface, &bld->base,
                                                              indir_vertex_index ? true : false,
                                                              indir_vertex_index ? indir_vertex_index : vertex_index_val,
                                                              (indir_index && !var->data.compact) ? true : false, attrib_index_val,
                                                              (indir_index && var->data.compact) ? true : false, swizzle_index_val);
               if (bit_size == 64) {
                  LLVMValueRef swizzle_index_val = lp_build_const_int32(gallivm, idx + 1);
                  result2 = bld->tes_iface->fetch_vertex_input(bld->tes_iface, &bld->base,
                                                               indir_vertex_index ? true : false,
                                                               indir_vertex_index ? indir_vertex_index : vertex_index_val,
                                                               indir_index ? true : false, attrib_index_val, false, swizzle_index_val);
                  result[i] = emit_fetch_64bit(bld, result[i], result2);
               }
            }
         } else if (bld->tcs_iface) {
            LLVMValueRef vertex_index_val = lp_build_const_int32(gallivm, vertex_index);
            LLVMValueRef attrib_index_val;
            LLVMValueRef swizzle_index_val = lp_build_const_int32(gallivm, idx);

            if (indir_index) {
               if (var->data.compact) {
                  swizzle_index_val = lp_build_add(&bld->uint_bld, indir_index, lp_build_const_int_vec(gallivm, bld->uint_bld.type, idx));
                  attrib_index_val = lp_build_const_int32(gallivm, comp_loc);
               } else
                  attrib_index_val = lp_build_add(&bld->uint_bld, indir_index, lp_build_const_int_vec(gallivm, bld->uint_bld.type, comp_loc));
            } else
               attrib_index_val = lp_build_const_int32(gallivm, comp_loc);
            result[i] = bld->tcs_iface->emit_fetch_input(bld->tcs_iface, &bld->base,
                                                         indir_vertex_index ? true : false, indir_vertex_index ? indir_vertex_index : vertex_index_val,
                                                         (indir_index && !var->data.compact) ? true : false, attrib_index_val,
                                                         (indir_index && var->data.compact) ? true : false, swizzle_index_val);
            if (bit_size == 64) {
               LLVMValueRef swizzle_index_val = lp_build_const_int32(gallivm, idx + 1);
               LLVMValueRef result2 = bld->tcs_iface->emit_fetch_input(bld->tcs_iface, &bld->base,
                                                                       indir_vertex_index ? true : false, indir_vertex_index ? indir_vertex_index : vertex_index_val,
                                                                       indir_index ? true : false, attrib_index_val,
                                                                       false, swizzle_index_val);
               result[i] = emit_fetch_64bit(bld, result[i], result2);
            }
         } else {
            if (indir_index) {
               LLVMValueRef attrib_index_val = lp_build_add(&bld->uint_bld, indir_index, lp_build_const_int_vec(gallivm, bld->uint_bld.type, comp_loc));
               LLVMValueRef index_vec = get_soa_array_offsets(&bld->uint_bld,
                                                              attrib_index_val, 4, idx,
                                                              true);
               LLVMValueRef index_vec2 = NULL;
               LLVMTypeRef scalar_type = LLVMFloatTypeInContext(gallivm->context);
               LLVMValueRef inputs_array = LLVMBuildBitCast(gallivm->builder, bld->inputs_array, LLVMPointerType(scalar_type, 0), "");

               if (bit_size == 64)
                  index_vec2 = get_soa_array_offsets(&bld->uint_bld,
                                                     indir_index, 4, idx + 1, true);

               /* Gather values from the input register array */
               result[i] = build_gather(bld, &bld->base, scalar_type, inputs_array, index_vec, NULL, index_vec2);
            } else {
               if (bld->indirects & nir_var_shader_in) {
                  LLVMValueRef lindex = lp_build_const_int32(gallivm,
                                                             comp_loc * 4 + idx);
                  LLVMValueRef input_ptr = lp_build_pointer_get2(gallivm->builder,
                                                                 bld->base.vec_type,
                                                                 bld->inputs_array, lindex);
                  if (bit_size == 64) {
                     LLVMValueRef lindex2 = lp_build_const_int32(gallivm,
                                                                 comp_loc * 4 + (idx + 1));
                     LLVMValueRef input_ptr2 = lp_build_pointer_get2(gallivm->builder,
                                                                     bld->base.vec_type,
                                                                     bld->inputs_array, lindex2);
                     result[i] = emit_fetch_64bit(bld, input_ptr, input_ptr2);
                  } else {
                     result[i] = input_ptr;
                  }
               } else {
                  if (bit_size == 64) {
                     LLVMValueRef tmp[2];
                     tmp[0] = bld->inputs[comp_loc][idx];
                     tmp[1] = bld->inputs[comp_loc][idx + 1];
                     result[i] = emit_fetch_64bit(bld, tmp[0], tmp[1]);
                  } else {
                     result[i] = bld->inputs[comp_loc][idx];
                  }
               }
            }
         }
      }
      break;
   case nir_var_shader_out:
      if (var->data.fb_fetch_output && bld->fs_iface && bld->fs_iface->fb_fetch) {
         bld->fs_iface->fb_fetch(bld->fs_iface, &bld->base, var->data.location, result);
         break;
      }

      for (unsigned i = 0; i < num_components; i++) {
         int idx = (i * dmul) + location_frac;
         int comp_loc = location;
         if (bit_size == 64 && idx >= 4) {
            comp_loc++;
            idx = idx % 4;
         }

         if (bld->tcs_iface) {
            LLVMValueRef vertex_index_val = lp_build_const_int32(gallivm, vertex_index);
            LLVMValueRef attrib_index_val;
            LLVMValueRef swizzle_index_val = lp_build_const_int32(gallivm, idx);

            if (indir_index)
               attrib_index_val = lp_build_add(&bld->uint_bld, indir_index, lp_build_const_int_vec(gallivm, bld->uint_bld.type, var->data.driver_location));
            else
               attrib_index_val = lp_build_const_int32(gallivm, comp_loc);

            result[i] = bld->tcs_iface->emit_fetch_output(bld->tcs_iface, &bld->base,
                                                          indir_vertex_index ? true : false, indir_vertex_index ? indir_vertex_index : vertex_index_val,
                                                          (indir_index && !var->data.compact) ? true : false, attrib_index_val,
                                                          (indir_index && var->data.compact) ? true : false, swizzle_index_val, 0);
            if (bit_size == 64) {
               LLVMValueRef swizzle_index_val = lp_build_const_int32(gallivm, idx + 1);
               LLVMValueRef result2 = bld->tcs_iface->emit_fetch_output(bld->tcs_iface, &bld->base,
                                                                        indir_vertex_index ? true : false, indir_vertex_index ? indir_vertex_index : vertex_index_val,
                                                                        indir_index ? true : false, attrib_index_val,
                                                                        false, swizzle_index_val, 0);
               result[i] = emit_fetch_64bit(bld, result[i], result2);
            }
         } else {
            /* Output variable behave like private variables during the shader execution.
               GLSL 4.60 spec, section 4.3.6.
               Vulkan 1.3 spec, Helper Invocations */
            if (bit_size == 64) {
               result[i] = emit_fetch_64bit(bld,
                     LLVMBuildLoad2(gallivm->builder, bld->base.vec_type, bld->outputs[comp_loc][idx], "output0_ptr"),
                     LLVMBuildLoad2(gallivm->builder, bld->base.vec_type, bld->outputs[comp_loc][idx + 1], "output1_ptr"));
            } else {
               result[i] = LLVMBuildLoad2(gallivm->builder, bld->base.vec_type, bld->outputs[comp_loc][idx], "output_ptr");
            }
         }
      }
      break;
   default:
      break;
   }
}

static void emit_store_chan(struct lp_build_nir_soa_context *bld,
                            nir_variable_mode deref_mode,
                            unsigned bit_size,
                            unsigned location, unsigned comp,
                            unsigned chan,
                            LLVMValueRef dst)
{
   LLVMBuilderRef builder = bld->base.gallivm->builder;
   struct lp_build_context *float_bld = &bld->base;

   if (bit_size == 64) {
      chan *= 2;
      chan += comp;
      if (chan >= 4) {
         chan -= 4;
         location++;
      }
      emit_store_64bit_chan(bld, bld->outputs[location][chan],
                            bld->outputs[location][chan + 1], dst);
   } else {
      dst = LLVMBuildBitCast(builder, dst, float_bld->vec_type, "");
      lp_exec_mask_store(&bld->exec_mask, float_bld, dst,
                         bld->outputs[location][chan + comp]);
   }
}

static void emit_store_tcs_chan(struct lp_build_nir_soa_context *bld,
                                bool is_compact,
                                unsigned bit_size,
                                unsigned location,
                                unsigned const_index,
                                LLVMValueRef indir_vertex_index,
                                LLVMValueRef indir_index,
                                unsigned comp,
                                unsigned chan,
                                LLVMValueRef chan_val)
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = bld->base.gallivm->builder;
   unsigned swizzle = chan;
   if (bit_size == 64) {
      swizzle *= 2;
      swizzle += comp;
      if (swizzle >= 4) {
         swizzle -= 4;
         location++;
      }
   } else
      swizzle += comp;
   LLVMValueRef attrib_index_val;
   LLVMValueRef swizzle_index_val = lp_build_const_int32(gallivm, swizzle);

   if (indir_index) {
      if (is_compact) {
         swizzle_index_val = lp_build_add(&bld->uint_bld, indir_index, lp_build_const_int_vec(gallivm, bld->uint_bld.type, swizzle));
         attrib_index_val = lp_build_const_int32(gallivm, const_index + location);
      } else
         attrib_index_val = lp_build_add(&bld->uint_bld, indir_index, lp_build_const_int_vec(gallivm, bld->uint_bld.type, location));
   } else
      attrib_index_val = lp_build_const_int32(gallivm, const_index + location);
   LLVMValueRef exec_mask = mask_vec(bld);
   if (bit_size == 64) {
      LLVMValueRef split_vals[2];
      LLVMValueRef swizzle_index_val2 = lp_build_const_int32(gallivm, swizzle + 1);
      emit_store_64bit_split(bld, chan_val, split_vals);
      if (bld->mesh_iface) {
         bld->mesh_iface->emit_store_output(bld->mesh_iface, &bld->base, 0,
                                           indir_vertex_index ? true : false,
                                           indir_vertex_index,
                                           indir_index ? true : false,
                                           attrib_index_val,
                                           false, swizzle_index_val,
                                           split_vals[0], exec_mask);
         bld->mesh_iface->emit_store_output(bld->mesh_iface, &bld->base, 0,
                                           indir_vertex_index ? true : false,
                                           indir_vertex_index,
                                           indir_index ? true : false,
                                           attrib_index_val,
                                           false, swizzle_index_val2,
                                           split_vals[1], exec_mask);
      } else {
         bld->tcs_iface->emit_store_output(bld->tcs_iface, &bld->base, 0,
                                           indir_vertex_index ? true : false,
                                           indir_vertex_index,
                                           indir_index ? true : false,
                                           attrib_index_val,
                                           false, swizzle_index_val,
                                           split_vals[0], exec_mask);
         bld->tcs_iface->emit_store_output(bld->tcs_iface, &bld->base, 0,
                                           indir_vertex_index ? true : false,
                                           indir_vertex_index,
                                           indir_index ? true : false,
                                           attrib_index_val,
                                           false, swizzle_index_val2,
                                           split_vals[1], exec_mask);
      }
   } else {
      chan_val = LLVMBuildBitCast(builder, chan_val, bld->base.vec_type, "");
      if (bld->mesh_iface) {
         bld->mesh_iface->emit_store_output(bld->mesh_iface, &bld->base, 0,
                                           indir_vertex_index ? true : false,
                                           indir_vertex_index,
                                           indir_index && !is_compact ? true : false,
                                           attrib_index_val,
                                           indir_index && is_compact ? true : false,
                                           swizzle_index_val,
                                           chan_val, exec_mask);
      } else {
         bld->tcs_iface->emit_store_output(bld->tcs_iface, &bld->base, 0,
                                           indir_vertex_index ? true : false,
                                           indir_vertex_index,
                                           indir_index && !is_compact ? true : false,
                                           attrib_index_val,
                                           indir_index && is_compact ? true : false,
                                           swizzle_index_val,
                                           chan_val, exec_mask);
      }
   }
}

static void emit_store_mesh_chan(struct lp_build_nir_soa_context *bld,
                                 bool is_compact,
                                 unsigned bit_size,
                                 unsigned location,
                                 unsigned const_index,
                                 LLVMValueRef indir_vertex_index,
                                 LLVMValueRef indir_index,
                                 unsigned comp,
                                 unsigned chan,
                                 LLVMValueRef chan_val)
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = bld->base.gallivm->builder;
   unsigned swizzle = chan;
   if (bit_size == 64) {
      swizzle += const_index;
      swizzle *= 2;
      swizzle += comp;
      if (swizzle >= 4) {
         swizzle -= 4;
         location++;
      }
   } else
      swizzle += comp;
   LLVMValueRef attrib_index_val;
   LLVMValueRef swizzle_index_val = lp_build_const_int32(gallivm, swizzle);

   if (indir_index) {
      if (is_compact) {
         swizzle_index_val = lp_build_add(&bld->uint_bld, indir_index, lp_build_const_int_vec(gallivm, bld->uint_bld.type, swizzle));
         attrib_index_val = lp_build_const_int32(gallivm, location);
      } else
         attrib_index_val = lp_build_add(&bld->uint_bld, indir_index, lp_build_const_int_vec(gallivm, bld->uint_bld.type, location));
   } else
      attrib_index_val = lp_build_const_int32(gallivm, location + const_index);
   LLVMValueRef exec_mask = mask_vec(bld);
   if (bit_size == 64) {
      LLVMValueRef split_vals[2];
      LLVMValueRef swizzle_index_val2 = lp_build_const_int32(gallivm, swizzle + 1);
      emit_store_64bit_split(bld, chan_val, split_vals);
      bld->mesh_iface->emit_store_output(bld->mesh_iface, &bld->base, 0,
                                         indir_vertex_index ? true : false,
                                         indir_vertex_index,
                                         indir_index ? true : false,
                                         attrib_index_val,
                                         false, swizzle_index_val,
                                         split_vals[0], exec_mask);
      bld->mesh_iface->emit_store_output(bld->mesh_iface, &bld->base, 0,
                                         indir_vertex_index ? true : false,
                                         indir_vertex_index,
                                         indir_index ? true : false,
                                         attrib_index_val,
                                         false, swizzle_index_val2,
                                         split_vals[1], exec_mask);
   } else {
      chan_val = LLVMBuildBitCast(builder, chan_val, bld->base.vec_type, "");
      bld->mesh_iface->emit_store_output(bld->mesh_iface, &bld->base, 0,
                                         indir_vertex_index ? true : false,
                                         indir_vertex_index,
                                         indir_index && !is_compact ? true : false,
                                         attrib_index_val,
                                         indir_index && is_compact ? true : false,
                                         swizzle_index_val,
                                         chan_val, exec_mask);
   }
}

static void emit_store_var(struct lp_build_nir_soa_context *bld,
                           nir_variable_mode deref_mode,
                           unsigned num_components,
                           unsigned bit_size,
                           nir_variable *var,
                           unsigned writemask,
                           LLVMValueRef indir_vertex_index,
                           unsigned const_index,
                           LLVMValueRef indir_index,
                           LLVMValueRef *dst)
{
   switch (deref_mode) {
   case nir_var_shader_out: {
      unsigned location = var->data.driver_location;
      unsigned comp = var->data.location_frac;
      if (bld->shader->info.stage == MESA_SHADER_FRAGMENT) {
         if (var->data.location == FRAG_RESULT_STENCIL)
            comp = 1;
         else if (var->data.location == FRAG_RESULT_DEPTH)
            comp = 2;
      }

      if (var->data.compact) {
         location += const_index / 4;
         comp += const_index % 4;
         const_index = 0;
      }

      for (unsigned chan = 0; chan < num_components; chan++) {
         if (writemask & (1u << chan)) {
            LLVMValueRef chan_val = dst[chan];
            if (bld->mesh_iface) {
               emit_store_mesh_chan(bld, var->data.compact, bit_size, location, const_index, indir_vertex_index, indir_index, comp, chan, chan_val);
            } else if (bld->tcs_iface) {
               emit_store_tcs_chan(bld, var->data.compact, bit_size, location, const_index, indir_vertex_index, indir_index, comp, chan, chan_val);
            } else
               emit_store_chan(bld, deref_mode, bit_size, location + const_index, comp, chan, chan_val);
         }
      }
      break;
   }
   default:
      break;
   }
}

/**
 * Returns the address of the given constant array index and channel in a
 * nir register.
 */
static LLVMValueRef reg_chan_pointer(struct lp_build_nir_soa_context *bld,
                                     struct lp_build_context *reg_bld,
                                     const nir_intrinsic_instr *decl,
                                     LLVMValueRef reg_storage,
                                     int array_index, int chan)
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   int nc = nir_intrinsic_num_components(decl);
   int num_array_elems = nir_intrinsic_num_array_elems(decl);

   LLVMTypeRef chan_type = reg_bld->vec_type;
   if (nc > 1)
      chan_type = LLVMArrayType(chan_type, nc);

   if (num_array_elems > 0) {
      LLVMTypeRef array_type = LLVMArrayType(chan_type, num_array_elems);
      reg_storage = lp_build_array_get_ptr2(gallivm, array_type, reg_storage,
                                            lp_build_const_int32(gallivm, array_index));
   }
   if (nc > 1) {
      reg_storage = lp_build_array_get_ptr2(gallivm, chan_type, reg_storage,
                                            lp_build_const_int32(gallivm, chan));
   }

   return reg_storage;
}

static LLVMValueRef global_addr_to_ptr(struct gallivm_state *gallivm, LLVMValueRef addr_ptr, unsigned bit_size)
{
   LLVMBuilderRef builder = gallivm->builder;
   switch (bit_size) {
   case 8:
      addr_ptr = LLVMBuildIntToPtr(builder, addr_ptr, LLVMPointerType(LLVMInt8TypeInContext(gallivm->context), 0), "");
      break;
   case 16:
      addr_ptr = LLVMBuildIntToPtr(builder, addr_ptr, LLVMPointerType(LLVMInt16TypeInContext(gallivm->context), 0), "");
      break;
   case 32:
   default:
      addr_ptr = LLVMBuildIntToPtr(builder, addr_ptr, LLVMPointerType(LLVMInt32TypeInContext(gallivm->context), 0), "");
      break;
   case 64:
      addr_ptr = LLVMBuildIntToPtr(builder, addr_ptr, LLVMPointerType(LLVMInt64TypeInContext(gallivm->context), 0), "");
      break;
   }
   return addr_ptr;
}

static LLVMValueRef global_addr_to_ptr_vec(struct gallivm_state *gallivm, LLVMValueRef addr_ptr, unsigned length, unsigned bit_size)
{
   LLVMBuilderRef builder = gallivm->builder;
   switch (bit_size) {
   case 8:
      addr_ptr = LLVMBuildIntToPtr(builder, addr_ptr, LLVMVectorType(LLVMPointerType(LLVMInt8TypeInContext(gallivm->context), 0), length), "");
      break;
   case 16:
      addr_ptr = LLVMBuildIntToPtr(builder, addr_ptr, LLVMVectorType(LLVMPointerType(LLVMInt16TypeInContext(gallivm->context), 0), length), "");
      break;
   case 32:
   default:
      addr_ptr = LLVMBuildIntToPtr(builder, addr_ptr, LLVMVectorType(LLVMPointerType(LLVMInt32TypeInContext(gallivm->context), 0), length), "");
      break;
   case 64:
      addr_ptr = LLVMBuildIntToPtr(builder, addr_ptr, LLVMVectorType(LLVMPointerType(LLVMInt64TypeInContext(gallivm->context), 0), length), "");
      break;
   }
   return addr_ptr;
}

static bool
lp_value_is_divergent(LLVMValueRef value)
{
   if (!value)
      return false;

   return LLVMGetTypeKind(LLVMTypeOf(value)) == LLVMVectorTypeKind;
}

static LLVMValueRef lp_vec_add_offset_ptr(struct lp_build_nir_soa_context *bld,
                                          unsigned bit_size,
                                          LLVMValueRef ptr,
                                          LLVMValueRef offset)
{
   unsigned pointer_size = 8 * sizeof(void *);
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;
   struct lp_build_context *ptr_bld = get_int_bld(
      bld, true, pointer_size, lp_value_is_divergent(ptr) || lp_value_is_divergent(offset));
   LLVMValueRef result = LLVMBuildPtrToInt(builder, ptr, ptr_bld->vec_type, "");
   if (pointer_size == 64)
      offset = LLVMBuildZExt(builder, offset, ptr_bld->vec_type, "");
   result = LLVMBuildAdd(builder, offset, result, "");
   return global_addr_to_ptr_vec(gallivm, result, ptr_bld->type.length, bit_size);
}

/* Returns a boolean for whether the offset is in range of the given limit for
 * SSBO/UBO dereferences.
 */
static LLVMValueRef
lp_offset_in_range(struct lp_build_nir_soa_context *bld,
                   LLVMValueRef offset,
                   LLVMValueRef limit)
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;

   LLVMValueRef fetch_extent = LLVMBuildAdd(builder, offset, lp_build_const_int32(gallivm, 1), "");
   LLVMValueRef fetch_in_bounds = LLVMBuildICmp(gallivm->builder, LLVMIntUGE, limit, fetch_extent, "");
   LLVMValueRef fetch_non_negative = LLVMBuildICmp(gallivm->builder, LLVMIntSGE, offset, lp_build_const_int32(gallivm, 0), "");
   return LLVMBuildAnd(gallivm->builder, fetch_in_bounds, fetch_non_negative, "");
}

static LLVMValueRef
load_ubo_base_addr(struct lp_build_nir_soa_context *bld, LLVMValueRef index)
{
   struct gallivm_state *gallivm = bld->base.gallivm;

   LLVMValueRef base = lp_llvm_buffer_base(gallivm, bld->consts_ptr, index, LP_MAX_TGSI_CONST_BUFFERS);
   base = LLVMBuildPtrToInt(gallivm->builder, base, LLVMInt64TypeInContext(gallivm->context), "");
   return base;
}

static void
emit_load_const(struct lp_build_nir_soa_context *bld,
                const nir_load_const_instr *instr,
                LLVMValueRef outval[NIR_MAX_VEC_COMPONENTS])
{
   struct lp_build_context *int_bld = get_int_bld(bld, true, instr->def.bit_size, false);
   const unsigned bits = instr->def.bit_size;

   for (unsigned i = 0; i < instr->def.num_components; i++) {
     outval[i] = lp_build_const_int_vec(bld->base.gallivm, int_bld->type,
                                        bits == 8 ? instr->value[i].u8 :
                                        bits == 16 ? instr->value[i].u32 :
                                        bits == 32 ? instr->value[i].u32 :
                                                     instr->value[i].u64);
   }
   for (unsigned i = instr->def.num_components; i < NIR_MAX_VEC_COMPONENTS; i++) {
      outval[i] = NULL;
   }
}

/**
 * Get the base address of SSBO[@index] for the @invocation channel, returning
 * the address and also the bounds (in units of the bit_size).
 */
static LLVMValueRef
ssbo_base_pointer(struct lp_build_nir_soa_context *bld,
                  unsigned bit_size,
                  LLVMValueRef index, LLVMValueRef invocation, LLVMValueRef *bounds)
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   uint32_t shift_val = bit_size_to_shift_size(bit_size);

   LLVMValueRef ssbo_idx = invocation ? LLVMBuildExtractElement(gallivm->builder, index, invocation, "") : index;

   LLVMValueRef ssbo_size_ptr = lp_llvm_buffer_num_elements(gallivm, bld->ssbo_ptr, ssbo_idx, LP_MAX_TGSI_SHADER_BUFFERS);
   LLVMValueRef ssbo_ptr = lp_llvm_buffer_base(gallivm, bld->ssbo_ptr, ssbo_idx, LP_MAX_TGSI_SHADER_BUFFERS);
   if (bounds)
      *bounds = LLVMBuildAShr(gallivm->builder, ssbo_size_ptr, lp_build_const_int32(gallivm, shift_val), "");

   return ssbo_ptr;
}

static LLVMValueRef
mem_access_base_pointer(struct lp_build_nir_soa_context *bld,
                        struct lp_build_context *mem_bld,
                        unsigned bit_size, bool payload,
                        LLVMValueRef index, LLVMValueRef invocation, LLVMValueRef *bounds)
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMValueRef ptr;

   if (index) {
      ptr = ssbo_base_pointer(bld, bit_size, index, invocation, bounds);
   } else {
      if (payload) {
         ptr = bld->payload_ptr;
         ptr = LLVMBuildPtrToInt(gallivm->builder, ptr, bld->int64_bld.elem_type, "");
         ptr = LLVMBuildAdd(gallivm->builder, ptr, lp_build_const_int64(gallivm, 12), "");
         ptr = LLVMBuildIntToPtr(gallivm->builder, ptr, LLVMPointerType(LLVMInt32TypeInContext(gallivm->context), 0), "");
         if (bounds)
            *bounds = NULL;
      }
      else {
         ptr = bld->shared_ptr;
         if (bounds) {
            /*
             * For shared mem, use essentially the same logic as for SSBOs,
             * and clamp against the declared size.
             * Note this is probably overkill - unsure of vulkan semantics,
             * but d3d would allow for instance to just clamp offsets,
             * since OOB writes cause shared mem to become undefined, and OOB
             * reads have undefined results, unlike for SSBOs there's no
             * need to ignore writes and return 0 for reads.
             * Bounds is in units of bit_size (not bytes).
             */
            uint32_t shift_val = bit_size_to_shift_size(bit_size);
            *bounds = LLVMBuildAShr(gallivm->builder, bld->shared_size,
                                    lp_build_const_int32(gallivm, shift_val), "");
         }
      }
   }

   /* Cast it to the pointer type of the access this instruction is doing. */
   if (bit_size == 32 && !mem_bld->type.floating)
      return ptr;
   else
      return LLVMBuildBitCast(gallivm->builder, ptr, LLVMPointerType(mem_bld->elem_type, 0), "");
}

static void emit_load_mem(struct lp_build_nir_soa_context *bld,
                          unsigned nc,
                          unsigned bit_size,
                          bool index_uniform, bool offset_uniform,
                          bool payload, bool in_bounds,
                          LLVMValueRef index,
                          LLVMValueRef offset,
                          LLVMValueRef outval[NIR_MAX_VEC_COMPONENTS])
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = bld->base.gallivm->builder;
   struct lp_build_context *uint_bld = get_int_bld(bld, true, 32, !index_uniform || !offset_uniform);
   struct lp_build_context *load_bld = get_int_bld(bld, true, bit_size, !index_uniform || !offset_uniform);

   uint32_t shift_val = bit_size_to_shift_size(bit_size);
   offset = LLVMBuildAShr(gallivm->builder, offset, lp_build_const_int_vec(gallivm, uint_bld->type, shift_val), "");

   /* If the address is uniform, then use the address from the first active
    * invocation 0 to load, and broadcast to all invocations.  We can't do
    * computed first active invocation for shared accesses (index == NULL),
    * though, since those don't do bounds checking and we could use an invalid
    * offset if exec_mask == 0.
    */
   if (index_uniform && offset_uniform) {
      LLVMValueRef ssbo_limit = NULL;
      LLVMValueRef mem_ptr = mem_access_base_pointer(bld, load_bld, bit_size, payload, index,
                                                     NULL, in_bounds ? NULL : &ssbo_limit);

      for (unsigned c = 0; c < nc; c++) {
         LLVMValueRef chan_offset = LLVMBuildAdd(builder, offset, lp_build_const_int32(gallivm, c), "");

         LLVMValueRef scalar;
         /* If loading outside the SSBO, we need to skip the load and read 0 instead. */
         if (ssbo_limit) {
            LLVMValueRef in_range = lp_offset_in_range(bld, chan_offset, ssbo_limit);
            LLVMValueRef ptr = LLVMBuildGEP2(builder, load_bld->elem_type, mem_ptr, &chan_offset, 1, "");
            LLVMValueRef null_ptr = LLVMBuildBitCast(builder, bld->null_qword_ptr, LLVMTypeOf(ptr), "");
            ptr = LLVMBuildSelect(builder, in_range, ptr, null_ptr, "");

            scalar = LLVMBuildLoad2(builder, load_bld->elem_type, ptr, "");
         } else {
            scalar = lp_build_pointer_get2(builder, load_bld->elem_type, mem_ptr, chan_offset);
         }

         outval[c] = scalar;
      }
      return;
   }

   LLVMValueRef gather_mask = mask_vec_with_helpers(bld);
   LLVMValueRef gather_cond = LLVMBuildICmp(gallivm->builder, LLVMIntNE, gather_mask, uint_bld->zero, "");

   if (index_uniform) {
      LLVMValueRef limit = NULL;
      LLVMValueRef mem_ptr = mem_access_base_pointer(bld, load_bld, bit_size, payload, index,
                                                     NULL, in_bounds ? NULL : &limit);

      if (limit)
         limit = lp_build_broadcast_scalar(uint_bld, limit);

      for (unsigned c = 0; c < nc; c++) {
         LLVMValueRef channel_offset = LLVMBuildAdd(builder, offset, lp_build_const_int_vec(gallivm, uint_bld->type, c), "channel_offset");
         LLVMValueRef channel_ptr = LLVMBuildGEP2(builder, load_bld->elem_type, mem_ptr, &channel_offset, 1, "channel_ptr");

         LLVMValueRef mask = gather_cond;
         if (limit) {
            LLVMValueRef oob_cmp = LLVMBuildICmp(builder, LLVMIntULT, channel_offset, limit, "oob_cmp");
            mask = LLVMBuildAnd(builder, mask, oob_cmp, "mask");
         }

         outval[c] = lp_build_masked_gather(gallivm, load_bld->type.length, load_bld->type.width, load_bld->vec_type,
                                            channel_ptr, mask);
      }

      return;
   }

   LLVMValueRef result[NIR_MAX_VEC_COMPONENTS];
   for (unsigned c = 0; c < nc; c++)
      result[c] = lp_build_alloca(gallivm, load_bld->vec_type, "");

   for (unsigned i = 0; i < uint_bld->type.length; i++) {
      LLVMValueRef counter = lp_build_const_int32(gallivm, i);
      LLVMValueRef element_gather_cond = LLVMBuildExtractElement(gallivm->builder, gather_cond, counter, "");

      struct lp_build_if_state if_gather_element;
      lp_build_if(&if_gather_element, gallivm, element_gather_cond);

      LLVMValueRef ssbo_limit = NULL;
      LLVMValueRef mem_ptr = mem_access_base_pointer(bld, load_bld, bit_size, payload, index,
                                                     counter, in_bounds ? NULL : &ssbo_limit);

      LLVMValueRef loop_offset = LLVMBuildExtractElement(gallivm->builder, offset, counter, "");

      for (unsigned c = 0; c < nc; c++) {
         LLVMValueRef loop_index = LLVMBuildAdd(builder, loop_offset, lp_build_const_int32(gallivm, c), "");
         LLVMValueRef do_fetch = lp_build_const_int32(gallivm, -1);
         if (ssbo_limit) {
            LLVMValueRef ssbo_oob_cmp = lp_build_compare(gallivm, lp_elem_type(uint_bld->type), PIPE_FUNC_LESS, loop_index, ssbo_limit);
            do_fetch = LLVMBuildAnd(builder, do_fetch, ssbo_oob_cmp, "");
         }

         struct lp_build_if_state ifthen;
         LLVMValueRef fetch_cond, temp_res;

         fetch_cond = LLVMBuildICmp(gallivm->builder, LLVMIntNE, do_fetch, lp_build_const_int32(gallivm, 0), "");

         lp_build_if(&ifthen, gallivm, fetch_cond);
         LLVMValueRef scalar = lp_build_pointer_get2(builder, load_bld->elem_type, mem_ptr, loop_index);

         temp_res = LLVMBuildLoad2(builder, load_bld->vec_type, result[c], "");
         temp_res = LLVMBuildInsertElement(builder, temp_res, scalar, counter, "");
         LLVMBuildStore(builder, temp_res, result[c]);
         lp_build_else(&ifthen);
         temp_res = LLVMBuildLoad2(builder, load_bld->vec_type, result[c], "");
         LLVMValueRef zero = lp_build_zero_bits(gallivm, bit_size, false);
         temp_res = LLVMBuildInsertElement(builder, temp_res, zero, counter, "");
         LLVMBuildStore(builder, temp_res, result[c]);
         lp_build_endif(&ifthen);
      }

      lp_build_endif(&if_gather_element);
   }
   for (unsigned c = 0; c < nc; c++)
      outval[c] = LLVMBuildLoad2(gallivm->builder, load_bld->vec_type, result[c], "");

}

static void emit_store_mem(struct lp_build_nir_soa_context *bld,
                           unsigned writemask,
                           unsigned nc,
                           unsigned bit_size,
                           bool payload,
                           bool in_bounds,
                           LLVMValueRef index,
                           LLVMValueRef offset,
                           LLVMValueRef *dst)
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = bld->base.gallivm->builder;
   struct lp_build_context *uint_bld = get_int_bld(bld, true, 32, lp_value_is_divergent(offset));
   struct lp_build_context *store_bld;
   uint32_t shift_val = bit_size_to_shift_size(bit_size);
   store_bld = get_int_bld(bld, true, bit_size, lp_value_is_divergent(index) || lp_value_is_divergent(offset));

   offset = lp_build_shr_imm(uint_bld, offset, shift_val);

   LLVMValueRef exec_mask = mask_vec(bld);
   LLVMValueRef cond = LLVMBuildICmp(gallivm->builder, LLVMIntNE, exec_mask, bld->uint_bld.zero, "");

   /* If the address is uniform, then just store the value from the first
    * channel instead of making LLVM unroll the invocation loop.  Note that we
    * don't use first_active_uniform(), since we aren't guaranteed that there is
    * actually an active invocation.
    */
   if (!lp_value_is_divergent(index) && !lp_value_is_divergent(offset)) {
      cond = LLVMBuildBitCast(builder, cond, LLVMIntTypeInContext(gallivm->context, bld->base.type.length), "exec_bitmask");
      cond = LLVMBuildZExt(builder, cond, bld->int_bld.elem_type, "");

      LLVMValueRef any_active = LLVMBuildICmp(builder, LLVMIntNE, cond, lp_build_const_int32(gallivm, 0), "any_active");

      LLVMValueRef ssbo_limit = NULL;
      LLVMValueRef mem_ptr = mem_access_base_pointer(bld, store_bld, bit_size, payload, index,
                                                     NULL, in_bounds ? NULL : &ssbo_limit);

      for (unsigned c = 0; c < nc; c++) {
         if (!(writemask & (1u << c)))
            continue;

         LLVMValueRef value_ptr = LLVMBuildBitCast(gallivm->builder, dst[c], store_bld->elem_type, "");

         LLVMValueRef chan_offset = LLVMBuildAdd(builder, offset, lp_build_const_int32(gallivm, c), "");
         LLVMValueRef ptr = LLVMBuildGEP2(builder, store_bld->elem_type, mem_ptr, &chan_offset, 1, "");

         LLVMValueRef valid_store = any_active;
         /* If storing outside the SSBO, we need to skip the store instead. */
         if (ssbo_limit)
            valid_store = LLVMBuildAnd(builder, valid_store, lp_offset_in_range(bld, chan_offset, ssbo_limit), "");

         LLVMValueRef noop_ptr = LLVMBuildBitCast(builder, bld->noop_store_ptr, LLVMTypeOf(ptr), "");
         ptr = LLVMBuildSelect(builder, valid_store, ptr, noop_ptr, "");
         LLVMBuildStore(builder, value_ptr, ptr);
      }
      return;
   }

   if (!lp_value_is_divergent(index)) {
      LLVMValueRef limit = NULL;
      LLVMValueRef mem_ptr = mem_access_base_pointer(bld, store_bld, bit_size, payload, index,
                                                     NULL, in_bounds ? NULL : &limit);

      if (limit)
         limit = lp_build_broadcast_scalar(uint_bld, limit);

      for (unsigned c = 0; c < nc; c++) {
         if (!(writemask & (1u << c)))
            continue;

         LLVMValueRef channel_offset = LLVMBuildAdd(builder, offset, lp_build_const_int_vec(gallivm, uint_bld->type, c), "channel_offset");
         LLVMValueRef channel_ptr = LLVMBuildGEP2(builder, store_bld->elem_type, mem_ptr, &channel_offset, 1, "channel_ptr");

         LLVMValueRef mask = cond;
         if (limit) {
            LLVMValueRef oob_cmp = LLVMBuildICmp(builder, LLVMIntULT, channel_offset, limit, "oob_cmp");
            mask = LLVMBuildAnd(builder, mask, oob_cmp, "mask");
         }

         LLVMValueRef value = LLVMBuildBitCast(gallivm->builder, dst[c], store_bld->vec_type, "");
         lp_build_masked_scatter(gallivm, store_bld->type.length, store_bld->type.width, channel_ptr, value, mask);
      }

      return;
   }

   for (unsigned i = 0; i < uint_bld->type.length; i++) {
      LLVMValueRef counter = lp_build_const_int32(gallivm, i);
      LLVMValueRef loop_cond = LLVMBuildExtractElement(gallivm->builder, cond, counter, "");

      struct lp_build_if_state exec_ifthen;
      lp_build_if(&exec_ifthen, gallivm, loop_cond);

      LLVMValueRef ssbo_limit = NULL;
      LLVMValueRef mem_ptr = mem_access_base_pointer(bld, store_bld, bit_size, payload, index,
                                                     counter, in_bounds ? NULL : &ssbo_limit);

      LLVMValueRef loop_offset = LLVMBuildExtractElement(gallivm->builder, offset, counter, "");

      for (unsigned c = 0; c < nc; c++) {
         if (!(writemask & (1u << c)))
            continue;
         LLVMValueRef loop_index = LLVMBuildAdd(builder, loop_offset, lp_build_const_int32(gallivm, c), "");
         LLVMValueRef do_store = lp_build_const_int32(gallivm, -1);

         if (ssbo_limit) {
            LLVMValueRef ssbo_oob_cmp = lp_build_compare(gallivm, lp_elem_type(uint_bld->type), PIPE_FUNC_LESS, loop_index, ssbo_limit);
            do_store = LLVMBuildAnd(builder, do_store, ssbo_oob_cmp, "");
         }

         LLVMValueRef value_ptr = LLVMBuildExtractElement(gallivm->builder, dst[c],
                                                          counter, "");
         value_ptr = LLVMBuildBitCast(gallivm->builder, value_ptr, store_bld->elem_type, "");
         struct lp_build_if_state ifthen;
         LLVMValueRef store_cond;

         store_cond = LLVMBuildICmp(gallivm->builder, LLVMIntNE, do_store, lp_build_const_int32(gallivm, 0), "");
         lp_build_if(&ifthen, gallivm, store_cond);
         lp_build_pointer_set(builder, mem_ptr, loop_index, value_ptr);
         lp_build_endif(&ifthen);
      }

      lp_build_endif(&exec_ifthen);
   }
}


static void emit_atomic_mem(struct lp_build_nir_soa_context *bld,
                            nir_atomic_op nir_op,
                            uint32_t bit_size,
                            bool payload,
                            bool in_bounds,
                            LLVMValueRef index, LLVMValueRef offset,
                            LLVMValueRef val, LLVMValueRef val2,
                            LLVMValueRef *result)
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = bld->base.gallivm->builder;
   struct lp_build_context *uint_bld = &bld->uint_bld;
   uint32_t shift_val = bit_size_to_shift_size(bit_size);
   bool is_float = nir_atomic_op_type(nir_op) == nir_type_float;
   struct lp_build_context *atomic_bld = is_float ? get_flt_bld(bld, bit_size, true) : get_int_bld(bld, true, bit_size, true);

   offset = lp_build_shr_imm(uint_bld, offset, shift_val);
   LLVMValueRef atom_res = lp_build_alloca(gallivm,
                                           atomic_bld->vec_type, "");

   LLVMValueRef exec_mask = mask_vec(bld);
   LLVMValueRef cond = LLVMBuildICmp(gallivm->builder, LLVMIntNE, exec_mask, uint_bld->zero, "");
   for (unsigned i = 0; i < uint_bld->type.length; i++) {
      LLVMValueRef counter = lp_build_const_int32(gallivm, i);
      LLVMValueRef loop_cond = LLVMBuildExtractElement(gallivm->builder, cond, counter, "");

      struct lp_build_if_state exec_ifthen;
      lp_build_if(&exec_ifthen, gallivm, loop_cond);

      LLVMValueRef ssbo_limit = NULL;
      LLVMValueRef mem_ptr = mem_access_base_pointer(bld, atomic_bld, bit_size, payload, index,
                                                     counter, in_bounds ? NULL : &ssbo_limit);

      LLVMValueRef loop_offset = LLVMBuildExtractElement(gallivm->builder, offset, counter, "");

      LLVMValueRef do_fetch = lp_build_const_int32(gallivm, -1);
      if (ssbo_limit) {
         LLVMValueRef ssbo_oob_cmp = lp_build_compare(gallivm, lp_elem_type(uint_bld->type), PIPE_FUNC_LESS, loop_offset, ssbo_limit);
         do_fetch = LLVMBuildAnd(builder, do_fetch, ssbo_oob_cmp, "");
      }

      LLVMValueRef value_ptr = LLVMBuildExtractElement(gallivm->builder, val,
                                                       counter, "");
      value_ptr = LLVMBuildBitCast(gallivm->builder, value_ptr, atomic_bld->elem_type, "");

      LLVMValueRef scalar_ptr = LLVMBuildGEP2(builder, atomic_bld->elem_type, mem_ptr, &loop_offset, 1, "");

      struct lp_build_if_state ifthen;
      LLVMValueRef inner_cond, temp_res;
      LLVMValueRef scalar;

      inner_cond = LLVMBuildICmp(gallivm->builder, LLVMIntNE, do_fetch, lp_build_const_int32(gallivm, 0), "");
      lp_build_if(&ifthen, gallivm, inner_cond);

      if (val2 != NULL) {
         LLVMValueRef cas_src_ptr = LLVMBuildExtractElement(gallivm->builder, val2,
                                                            counter, "");
         cas_src_ptr = LLVMBuildBitCast(gallivm->builder, cas_src_ptr, atomic_bld->elem_type, "");
         scalar = LLVMBuildAtomicCmpXchg(builder, scalar_ptr, value_ptr,
                                         cas_src_ptr,
                                         LLVMAtomicOrderingSequentiallyConsistent,
                                         LLVMAtomicOrderingSequentiallyConsistent,
                                         false);
         scalar = LLVMBuildExtractValue(gallivm->builder, scalar, 0, "");
      } else {
         scalar = LLVMBuildAtomicRMW(builder, lp_translate_atomic_op(nir_op),
                                     scalar_ptr, value_ptr,
                                     LLVMAtomicOrderingSequentiallyConsistent,
                                     false);
      }
      temp_res = LLVMBuildLoad2(builder, atomic_bld->vec_type, atom_res, "");
      temp_res = LLVMBuildInsertElement(builder, temp_res, scalar, counter, "");
      LLVMBuildStore(builder, temp_res, atom_res);
      lp_build_else(&ifthen);
      temp_res = LLVMBuildLoad2(builder, atomic_bld->vec_type, atom_res, "");
      LLVMValueRef zero = lp_build_zero_bits(gallivm, bit_size, is_float);
      temp_res = LLVMBuildInsertElement(builder, temp_res, zero, counter, "");
      LLVMBuildStore(builder, temp_res, atom_res);
      lp_build_endif(&ifthen);

      lp_build_endif(&exec_ifthen);
   }
   *result = LLVMBuildLoad2(builder, atomic_bld->vec_type, atom_res, "");
}

static void emit_image_op(struct lp_build_nir_soa_context *bld,
                          struct lp_img_params *params)
{
   params->type = bld->base.type;
   params->resources_type = bld->resources_type;
   params->resources_ptr = bld->resources_ptr;
   params->thread_data_type = bld->thread_data_type;
   params->thread_data_ptr = bld->thread_data_ptr;
   params->exec_mask = mask_vec(bld);
   params->exec_mask_nz = lp_exec_mask_is_nz(bld);

   bld->image->emit_op(bld->image,
                       bld->base.gallivm,
                       params);

}

static void emit_image_size(struct lp_build_nir_soa_context *bld,
                            struct lp_sampler_size_query_params *params)
{
   params->int_type = bld->int_bld.type;
   params->resources_type = bld->resources_type;
   params->resources_ptr = bld->resources_ptr;

   bld->image->emit_size_query(bld->image,
                               bld->base.gallivm,
                               params);

}

static void init_var_slots(struct lp_build_nir_soa_context *bld,
                           nir_variable *var, unsigned sc)
{
   unsigned slots = glsl_count_attribute_slots(var->type, false) * 4;

   if (!bld->outputs)
     return;
   for (unsigned comp = sc; comp < slots + sc; comp++) {
      unsigned this_loc = var->data.driver_location + (comp / 4);
      unsigned this_chan = comp % 4;

      if (!bld->outputs[this_loc][this_chan])
         bld->outputs[this_loc][this_chan] = lp_build_alloca(bld->base.gallivm,
                                                             bld->base.vec_type, "output");
   }
}

static void emit_var_decl(struct lp_build_nir_soa_context *bld,
                          nir_variable *var)
{
   unsigned sc = var->data.location_frac;
   switch (var->data.mode) {
   case nir_var_shader_out: {
      if (bld->shader->info.stage == MESA_SHADER_FRAGMENT) {
         if (var->data.location == FRAG_RESULT_STENCIL)
            sc = 1;
         else if (var->data.location == FRAG_RESULT_DEPTH)
            sc = 2;
      }
      init_var_slots(bld, var, sc);
      break;
   }
   default:
      break;
   }
}

static void emit_tex(struct lp_build_nir_soa_context *bld,
                     struct lp_sampler_params *params)
{
   struct gallivm_state *gallivm = bld->base.gallivm;

   params->type = bld->base.type;
   params->resources_type = bld->resources_type;
   params->resources_ptr = bld->resources_ptr;
   params->thread_data_type = bld->thread_data_type;
   params->thread_data_ptr = bld->thread_data_ptr;
   params->exec_mask = mask_vec(bld);
   params->exec_mask_nz = lp_exec_mask_is_nz(bld);

   if (params->texture_index_offset && bld->shader->info.stage != MESA_SHADER_FRAGMENT) {
      /* this is horrible but this can be dynamic */
      LLVMValueRef coords[5];
      LLVMValueRef *orig_texel_ptr;
      struct lp_build_context *uint_bld = &bld->uint_bld;
      LLVMValueRef result[4] = { LLVMGetUndef(bld->base.vec_type),
                                 LLVMGetUndef(bld->base.vec_type),
                                 LLVMGetUndef(bld->base.vec_type),
                                 LLVMGetUndef(bld->base.vec_type) };
      LLVMValueRef texel[4], orig_offset, orig_lod;
      unsigned i;
      orig_texel_ptr = params->texel;
      orig_lod = params->lod;
      for (i = 0; i < 5; i++) {
         coords[i] = params->coords[i];
      }
      orig_offset = params->texture_index_offset;

      for (unsigned v = 0; v < uint_bld->type.length; v++) {
         LLVMValueRef idx = lp_build_const_int32(gallivm, v);
         LLVMValueRef new_coords[5];
         for (i = 0; i < 5; i++) {
            new_coords[i] = LLVMBuildExtractElement(gallivm->builder,
                                                    coords[i], idx, "");
         }
         params->coords = new_coords;
         params->texture_index_offset = LLVMBuildExtractElement(gallivm->builder,
                                                                orig_offset,
                                                                idx, "");
         params->type = lp_elem_type(bld->base.type);

         if (orig_lod)
            params->lod = LLVMBuildExtractElement(gallivm->builder, orig_lod, idx, "");
         params->texel = texel;
         bld->sampler->emit_tex_sample(bld->sampler,
                                       gallivm,
                                       params);

         for (i = 0; i < 4; i++) {
            result[i] = LLVMBuildInsertElement(gallivm->builder, result[i], texel[i], idx, "");
         }
      }
      for (i = 0; i < 4; i++) {
         orig_texel_ptr[i] = result[i];
      }
      return;
   }

   if (params->texture_index_offset) {
      params->texture_index_offset = LLVMBuildExtractElement(gallivm->builder, params->texture_index_offset,
                                                             first_active_invocation(bld, true), "");
   }

   params->type = bld->base.type;
   bld->sampler->emit_tex_sample(bld->sampler,
                                 bld->base.gallivm,
                                 params);
}

static void emit_tex_size(struct lp_build_nir_soa_context *bld,
                          struct lp_sampler_size_query_params *params)
{
   params->int_type = bld->int_bld.type;
   params->resources_type = bld->resources_type;
   params->resources_ptr = bld->resources_ptr;
   if (params->texture_unit_offset)
      params->texture_unit_offset = LLVMBuildExtractElement(bld->base.gallivm->builder,
                                                            params->texture_unit_offset,
                                                            lp_build_const_int32(bld->base.gallivm, 0), "");

   params->exec_mask = mask_vec(bld);
   params->exec_mask_nz = lp_exec_mask_is_nz(bld);

   bld->sampler->emit_size_query(bld->sampler,
                                 bld->base.gallivm,
                                 params);
}

static LLVMValueRef get_local_invocation_index(struct lp_build_nir_soa_context *bld)
{
   LLVMValueRef tmp, tmp2;

   tmp = lp_build_broadcast_scalar(&bld->uint_bld, bld->system_values.block_size[1]);
   tmp2 = lp_build_broadcast_scalar(&bld->uint_bld, bld->system_values.block_size[0]);
   tmp = lp_build_mul(&bld->uint_bld, tmp, tmp2);
   tmp = lp_build_mul(&bld->uint_bld, tmp, bld->system_values.thread_id[2]);

   tmp2 = lp_build_mul(&bld->uint_bld, tmp2, bld->system_values.thread_id[1]);
   tmp = lp_build_add(&bld->uint_bld, tmp, tmp2);
   tmp = lp_build_add(&bld->uint_bld, tmp, bld->system_values.thread_id[0]);
   return tmp;
}

static void emit_sysval_intrin(struct lp_build_nir_soa_context *bld,
                               nir_intrinsic_instr *instr,
                               LLVMValueRef result[NIR_MAX_VEC_COMPONENTS])
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   switch (instr->intrinsic) {
   case nir_intrinsic_load_instance_id:
      result[0] = bld->system_values.instance_id;
      break;
   case nir_intrinsic_load_base_instance:
      result[0] = bld->system_values.base_instance;
      break;
   case nir_intrinsic_load_base_vertex:
      result[0] = bld->system_values.basevertex;
      break;
   case nir_intrinsic_load_first_vertex:
      result[0] = bld->system_values.firstvertex;
      break;
   case nir_intrinsic_load_vertex_id:
      result[0] = bld->system_values.vertex_id;
      break;
   case nir_intrinsic_load_primitive_id:
      result[0] = bld->system_values.prim_id;
      break;
   case nir_intrinsic_load_workgroup_id: {
      for (unsigned i = 0; i < 3; i++)
         result[i] = bld->system_values.block_id[i];
      break;
   }
   case nir_intrinsic_load_local_invocation_id:
      for (unsigned i = 0; i < 3; i++)
         result[i] = bld->system_values.thread_id[i];
      break;
   case nir_intrinsic_load_local_invocation_index:
      result[0] = get_local_invocation_index(bld);
      break;
   case nir_intrinsic_load_num_workgroups: {
      for (unsigned i = 0; i < 3; i++)
         result[i] = bld->system_values.grid_size[i];
      break;
   }
   case nir_intrinsic_load_invocation_id:
      result[0] = bld->system_values.invocation_id;
      break;
   case nir_intrinsic_load_front_face:
      result[0] = bld->system_values.front_facing;
      break;
   case nir_intrinsic_load_draw_id:
      result[0] = bld->system_values.draw_id;
      break;
   default:
      break;
   case nir_intrinsic_load_workgroup_size:
     for (unsigned i = 0; i < 3; i++)
       result[i] = bld->system_values.block_size[i];
     break;
   case nir_intrinsic_load_work_dim:
      result[0] = bld->system_values.work_dim;
      break;
   case nir_intrinsic_load_tess_coord:
      for (unsigned i = 0; i < 3; i++) {
	 result[i] = LLVMBuildExtractValue(gallivm->builder, bld->system_values.tess_coord, i, "");
      }
      break;
   case nir_intrinsic_load_tess_level_outer:
      for (unsigned i = 0; i < 4; i++)
         result[i] = LLVMBuildExtractValue(gallivm->builder, bld->system_values.tess_outer, i, "");
      break;
   case nir_intrinsic_load_tess_level_inner:
      for (unsigned i = 0; i < 2; i++)
         result[i] = LLVMBuildExtractValue(gallivm->builder, bld->system_values.tess_inner, i, "");
      break;
   case nir_intrinsic_load_patch_vertices_in:
      result[0] = bld->system_values.vertices_in;
      break;
   case nir_intrinsic_load_sample_id:
      result[0] = bld->system_values.sample_id;
      break;
   case nir_intrinsic_load_sample_pos:
      for (unsigned i = 0; i < 2; i++) {
         LLVMValueRef idx = LLVMBuildMul(gallivm->builder, bld->system_values.sample_id, lp_build_const_int32(gallivm, 2), "");
         idx = LLVMBuildAdd(gallivm->builder, idx, lp_build_const_int32(gallivm, i), "");
         result[i] = lp_build_array_get2(gallivm, bld->system_values.sample_pos_type,
                                         bld->system_values.sample_pos, idx);
      }
      break;
   case nir_intrinsic_load_sample_mask_in:
      result[0] = bld->system_values.sample_mask_in;
      break;
   case nir_intrinsic_load_view_index:
      result[0] = bld->system_values.view_index;
      break;
   case nir_intrinsic_load_subgroup_invocation: {
      LLVMValueRef elems[LP_MAX_VECTOR_LENGTH];
      for(unsigned i = 0; i < bld->base.type.length; ++i)
         elems[i] = lp_build_const_int32(gallivm, i);
      result[0] = LLVMConstVector(elems, bld->base.type.length);
      break;
   }
   case nir_intrinsic_load_subgroup_id:
      result[0] = bld->system_values.subgroup_id;
      break;
   case nir_intrinsic_load_num_subgroups:
      result[0] = bld->system_values.num_subgroups;
      break;
   }
}

static void emit_helper_invocation(struct lp_build_nir_soa_context *bld,
                                   LLVMValueRef *dst)
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   struct lp_build_context *uint_bld = &bld->uint_bld;
   *dst = LLVMBuildICmp(gallivm->builder, LLVMIntNE, mask_vec(bld), lp_build_const_int_vec(gallivm, uint_bld->type, -1), "");
}

static void lp_build_skip_branch(struct lp_build_nir_soa_context *bld, bool flatten)
{
   if (flatten)
      return;

   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;

   LLVMValueRef exec_mask = mask_vec_with_helpers(bld);
   LLVMValueRef bitmask = LLVMBuildICmp(builder, LLVMIntNE, exec_mask, bld->uint_bld.zero, "");
   bitmask = LLVMBuildBitCast(builder, bitmask, LLVMIntTypeInContext(gallivm->context, bld->uint_bld.type.length), "");
   bitmask = LLVMBuildZExt(builder, bitmask, bld->int_bld.elem_type, "");

   LLVMValueRef any_active = LLVMBuildICmp(builder, LLVMIntNE, bitmask, lp_build_const_int32(gallivm, 0), "any_active");

   if (bld->if_stack_size >= LP_MAX_TGSI_NESTING) {
      bld->if_stack_size++;
      return;
   }

   lp_build_if(&bld->if_stack[bld->if_stack_size], gallivm, any_active);
   bld->if_stack_size++;
}

static void lp_build_skip_branch_end(struct lp_build_nir_soa_context *bld, bool flatten)
{
   if (flatten)
      return;

   assert(bld->if_stack_size);
   bld->if_stack_size--;
   if (bld->if_stack_size >= LP_MAX_TGSI_NESTING)
      return;

   lp_build_endif(&bld->if_stack[bld->if_stack_size]);
}

static void if_cond(struct lp_build_nir_soa_context *bld, LLVMValueRef cond, bool flatten)
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;

   lp_exec_mask_cond_push(&bld->exec_mask, LLVMBuildSExt(builder, cond, bld->base.int_vec_type, ""));

   lp_build_skip_branch(bld, flatten);
}

static void else_stmt(struct lp_build_nir_soa_context *bld, bool flatten_then, bool flatten_else)
{
   lp_build_skip_branch_end(bld, flatten_then);
   lp_exec_mask_cond_invert(&bld->exec_mask);
   lp_build_skip_branch(bld, flatten_else);
}

static void endif_stmt(struct lp_build_nir_soa_context *bld, bool flatten)
{
   lp_build_skip_branch_end(bld, flatten);
   lp_exec_mask_cond_pop(&bld->exec_mask);
}

static void break_stmt(struct lp_build_nir_soa_context *bld)
{
   lp_exec_break(&bld->exec_mask, NULL, false);
}

static void continue_stmt(struct lp_build_nir_soa_context *bld)
{
   lp_exec_continue(&bld->exec_mask);
}

static void
increment_vec_ptr_by_mask(struct lp_build_nir_soa_context *bld,
                          LLVMValueRef ptr,
                          LLVMValueRef mask)
{
   LLVMBuilderRef builder = bld->base.gallivm->builder;
   LLVMValueRef current_vec = LLVMBuildLoad2(builder, LLVMTypeOf(mask), ptr, "");

   current_vec = LLVMBuildSub(builder, current_vec, mask, "");

   LLVMBuildStore(builder, current_vec, ptr);
}

static void
clear_uint_vec_ptr_from_mask(struct lp_build_nir_soa_context *bld,
                             LLVMValueRef ptr,
                             LLVMValueRef mask)
{
   LLVMBuilderRef builder = bld->base.gallivm->builder;
   LLVMValueRef current_vec = LLVMBuildLoad2(builder, bld->uint_bld.vec_type, ptr, "");

   current_vec = lp_build_select(&bld->uint_bld,
                                 mask,
                                 bld->uint_bld.zero,
                                 current_vec);

   LLVMBuildStore(builder, current_vec, ptr);
}

static LLVMValueRef
clamp_mask_to_max_output_vertices(struct lp_build_nir_soa_context *bld,
                                  LLVMValueRef current_mask_vec,
                                  LLVMValueRef total_emitted_vertices_vec)
{
   LLVMBuilderRef builder = bld->base.gallivm->builder;
   struct lp_build_context *int_bld = &bld->int_bld;
   LLVMValueRef max_mask = lp_build_cmp(int_bld, PIPE_FUNC_LESS,
                                            total_emitted_vertices_vec,
                                            bld->max_output_vertices_vec);

   return LLVMBuildAnd(builder, current_mask_vec, max_mask, "");
}

static void emit_vertex(struct lp_build_nir_soa_context *bld, uint32_t stream_id)
{
   LLVMBuilderRef builder = bld->base.gallivm->builder;

   if (stream_id >= bld->gs_vertex_streams)
      return;
   assert(bld->gs_iface->emit_vertex);
   LLVMValueRef total_emitted_vertices_vec =
      LLVMBuildLoad2(builder, bld->uint_bld.vec_type, bld->total_emitted_vertices_vec_ptr[stream_id], "");
   LLVMValueRef mask = mask_vec(bld);
   mask = clamp_mask_to_max_output_vertices(bld, mask, total_emitted_vertices_vec);
   bld->gs_iface->emit_vertex(bld->gs_iface, &bld->base,
                              bld->outputs,
                              total_emitted_vertices_vec,
                              mask,
                              lp_build_const_int_vec(bld->base.gallivm, bld->base.type, stream_id));

   increment_vec_ptr_by_mask(bld, bld->emitted_vertices_vec_ptr[stream_id],
                             mask);
   increment_vec_ptr_by_mask(bld, bld->total_emitted_vertices_vec_ptr[stream_id],
                             mask);
}

static void
end_primitive_masked(struct lp_build_nir_soa_context *bld,
                     LLVMValueRef mask, uint32_t stream_id)
{
   LLVMBuilderRef builder = bld->base.gallivm->builder;

   if (stream_id >= bld->gs_vertex_streams)
      return;
   struct lp_build_context *uint_bld = &bld->uint_bld;
   LLVMValueRef emitted_vertices_vec =
      LLVMBuildLoad2(builder, bld->uint_bld.vec_type, bld->emitted_vertices_vec_ptr[stream_id], "");
   LLVMValueRef emitted_prims_vec =
      LLVMBuildLoad2(builder, bld->uint_bld.vec_type, bld->emitted_prims_vec_ptr[stream_id], "");
   LLVMValueRef total_emitted_vertices_vec =
      LLVMBuildLoad2(builder, bld->uint_bld.vec_type, bld->total_emitted_vertices_vec_ptr[stream_id], "");

   LLVMValueRef emitted_mask = lp_build_cmp(uint_bld,
                                            PIPE_FUNC_NOTEQUAL,
                                            emitted_vertices_vec,
                                            uint_bld->zero);
   mask = LLVMBuildAnd(builder, mask, emitted_mask, "");
   bld->gs_iface->end_primitive(bld->gs_iface, &bld->base,
				total_emitted_vertices_vec,
				emitted_vertices_vec, emitted_prims_vec, mask, stream_id);
   increment_vec_ptr_by_mask(bld, bld->emitted_prims_vec_ptr[stream_id],
                             mask);
   clear_uint_vec_ptr_from_mask(bld, bld->emitted_vertices_vec_ptr[stream_id],
                                mask);
}

static void end_primitive(struct lp_build_nir_soa_context *bld, uint32_t stream_id)
{
   assert(bld->gs_iface->end_primitive);

   LLVMValueRef mask = mask_vec(bld);
   end_primitive_masked(bld, mask, stream_id);
}

static void
emit_prologue(struct lp_build_nir_soa_context *bld)
{
   struct gallivm_state * gallivm = bld->base.gallivm;
   if (bld->indirects & nir_var_shader_in && !bld->gs_iface && !bld->tcs_iface && !bld->tes_iface) {
      uint32_t num_inputs = bld->num_inputs;
      /* If this is an indirect case, the number of inputs should not be 0 */
      assert(num_inputs > 0);

      unsigned index, chan;
      LLVMTypeRef vec_type = bld->base.vec_type;
      LLVMValueRef array_size = lp_build_const_int32(gallivm, num_inputs * 4);
      bld->inputs_array = lp_build_array_alloca(gallivm,
                                               vec_type, array_size,
                                               "input_array");

      for (index = 0; index < num_inputs; ++index) {
         for (chan = 0; chan < TGSI_NUM_CHANNELS; ++chan) {
            LLVMValueRef lindex =
               lp_build_const_int32(gallivm, index * 4 + chan);
            LLVMValueRef input_ptr =
               LLVMBuildGEP2(gallivm->builder, vec_type, bld->inputs_array, &lindex, 1, "");
            LLVMValueRef value = bld->inputs[index][chan];
            if (value)
               LLVMBuildStore(gallivm->builder, value, input_ptr);
         }
      }
   }
}

static void emit_vote(struct lp_build_nir_soa_context *bld, LLVMValueRef src,
                      nir_intrinsic_instr *instr, LLVMValueRef result[4])
{
   struct gallivm_state * gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;
   uint32_t bit_size = nir_src_bit_size(instr->src[0]);
   LLVMValueRef exec_mask = group_op_mask_vec(bld);
   struct lp_build_loop_state loop_state;
   LLVMValueRef outer_cond = LLVMBuildICmp(builder, LLVMIntNE, exec_mask, bld->uint_bld.zero, "");

   LLVMValueRef res_store = lp_build_alloca(gallivm, bld->uint_bld.elem_type, "");
   LLVMValueRef eq_store = lp_build_alloca(gallivm, get_int_bld(bld, true, bit_size, false)->elem_type, "");
   LLVMValueRef init_val = NULL;
   if (instr->intrinsic == nir_intrinsic_vote_ieq ||
       instr->intrinsic == nir_intrinsic_vote_feq) {
      /* for equal we unfortunately have to loop and find the first valid one. */
      lp_build_loop_begin(&loop_state, gallivm, lp_build_const_int32(gallivm, 0));
      LLVMValueRef if_cond = LLVMBuildExtractElement(gallivm->builder, outer_cond, loop_state.counter, "");

      struct lp_build_if_state ifthen;
      lp_build_if(&ifthen, gallivm, if_cond);
      LLVMValueRef value_ptr = LLVMBuildExtractElement(gallivm->builder, src,
                                                       loop_state.counter, "");
      LLVMBuildStore(builder, value_ptr, eq_store);
      LLVMBuildStore(builder, lp_build_const_int32(gallivm, -1), res_store);
      lp_build_endif(&ifthen);
      lp_build_loop_end_cond(&loop_state, lp_build_const_int32(gallivm, bld->uint_bld.type.length),
                             NULL, LLVMIntUGE);
      init_val = LLVMBuildLoad2(builder, get_int_bld(bld, true, bit_size, false)->elem_type, eq_store, "");
   } else {
      LLVMBuildStore(builder, lp_build_const_int32(gallivm, instr->intrinsic == nir_intrinsic_vote_any ? 0 : -1), res_store);
   }

   if (bit_size == 1) {
      src = LLVMBuildSExt(builder, src, get_int_bld(bld, true, 32, lp_value_is_divergent(src))->vec_type, "");
      if (init_val)
         init_val = LLVMBuildSExt(builder, init_val, get_int_bld(bld, true, 32, lp_value_is_divergent(init_val))->vec_type, "");
   }

   LLVMValueRef res;
   lp_build_loop_begin(&loop_state, gallivm, lp_build_const_int32(gallivm, 0));
   LLVMValueRef value_ptr = LLVMBuildExtractElement(gallivm->builder, src,
                                                       loop_state.counter, "");
   struct lp_build_if_state ifthen;
   LLVMValueRef if_cond;
   if_cond = LLVMBuildExtractElement(gallivm->builder, outer_cond, loop_state.counter, "");

   lp_build_if(&ifthen, gallivm, if_cond);
   res = LLVMBuildLoad2(builder, bld->uint_bld.elem_type, res_store, "");

   if (instr->intrinsic == nir_intrinsic_vote_feq) {
      struct lp_build_context *flt_bld = get_flt_bld(bld, bit_size, false);
      LLVMValueRef tmp = LLVMBuildFCmp(builder, LLVMRealUEQ,
                                       LLVMBuildBitCast(builder, init_val, flt_bld->elem_type, ""),
                                       LLVMBuildBitCast(builder, value_ptr, flt_bld->elem_type, ""), "");
      tmp = LLVMBuildSExt(builder, tmp, bld->uint_bld.elem_type, "");
      res = LLVMBuildAnd(builder, res, tmp, "");
   } else if (instr->intrinsic == nir_intrinsic_vote_ieq) {
      LLVMValueRef tmp = LLVMBuildICmp(builder, LLVMIntEQ, init_val, value_ptr, "");
      tmp = LLVMBuildSExt(builder, tmp, bld->uint_bld.elem_type, "");
      res = LLVMBuildAnd(builder, res, tmp, "");
   } else if (instr->intrinsic == nir_intrinsic_vote_any)
      res = LLVMBuildOr(builder, res, value_ptr, "");
   else
      res = LLVMBuildAnd(builder, res, value_ptr, "");
   LLVMBuildStore(builder, res, res_store);
   lp_build_endif(&ifthen);
   lp_build_loop_end_cond(&loop_state, lp_build_const_int32(gallivm, bld->uint_bld.type.length),
                          NULL, LLVMIntUGE);
   result[0] = LLVMBuildLoad2(builder, bld->uint_bld.elem_type, res_store, "");
   result[0] = LLVMBuildICmp(builder, LLVMIntNE, result[0], lp_build_const_int32(gallivm, 0), "");
}

static void emit_ballot(struct lp_build_nir_soa_context *bld, LLVMValueRef src, nir_intrinsic_instr *instr, LLVMValueRef result[4])
{
   struct gallivm_state * gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;
   LLVMValueRef exec_mask = group_op_mask_vec(bld);
   struct lp_build_loop_state loop_state;
   src = LLVMBuildSExt(builder, src, bld->int_bld.vec_type, "");
   src = LLVMBuildAnd(builder, src, exec_mask, "");
   LLVMValueRef res_store = lp_build_alloca(gallivm, bld->int_bld.elem_type, "");
   LLVMValueRef res;
   lp_build_loop_begin(&loop_state, gallivm, lp_build_const_int32(gallivm, 0));
   LLVMValueRef value_ptr = LLVMBuildExtractElement(gallivm->builder, src,
                                                    loop_state.counter, "");
   res = LLVMBuildLoad2(builder, bld->int_bld.elem_type, res_store, "");
   res = LLVMBuildOr(builder,
                     res,
                     LLVMBuildAnd(builder, value_ptr, LLVMBuildShl(builder, lp_build_const_int32(gallivm, 1), loop_state.counter, ""), ""), "");
   LLVMBuildStore(builder, res, res_store);

   lp_build_loop_end_cond(&loop_state, lp_build_const_int32(gallivm, bld->uint_bld.type.length),
                          NULL, LLVMIntUGE);
   result[0] = LLVMBuildLoad2(builder, bld->int_bld.elem_type, res_store, "");
}

static void emit_elect(struct lp_build_nir_soa_context *bld, LLVMValueRef result[4])
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;
   LLVMValueRef exec_mask = group_op_mask_vec(bld);
   struct lp_build_loop_state loop_state;

   LLVMValueRef idx_store = lp_build_alloca(gallivm, bld->int_bld.elem_type, "");
   LLVMValueRef found_store = lp_build_alloca(gallivm, bld->int_bld.elem_type, "");
   lp_build_loop_begin(&loop_state, gallivm, lp_build_const_int32(gallivm, 0));
   LLVMValueRef value_ptr = LLVMBuildExtractElement(gallivm->builder, exec_mask,
                                                    loop_state.counter, "");
   LLVMValueRef cond = LLVMBuildICmp(gallivm->builder,
                                     LLVMIntEQ,
                                     value_ptr,
                                     lp_build_const_int32(gallivm, -1), "");
   LLVMValueRef cond2 = LLVMBuildICmp(gallivm->builder,
                                      LLVMIntEQ,
                                      LLVMBuildLoad2(builder, bld->int_bld.elem_type, found_store, ""),
                                      lp_build_const_int32(gallivm, 0), "");

   cond = LLVMBuildAnd(builder, cond, cond2, "");
   struct lp_build_if_state ifthen;
   lp_build_if(&ifthen, gallivm, cond);
   LLVMBuildStore(builder, lp_build_const_int32(gallivm, 1), found_store);
   LLVMBuildStore(builder, loop_state.counter, idx_store);
   lp_build_endif(&ifthen);
   lp_build_loop_end_cond(&loop_state, lp_build_const_int32(gallivm, bld->uint_bld.type.length),
                          NULL, LLVMIntUGE);

   result[0] = LLVMBuildInsertElement(builder, bld->uint_bld.zero,
                                      lp_build_const_int32(gallivm, -1),
                                      LLVMBuildLoad2(builder, bld->int_bld.elem_type, idx_store, ""),
                                      "");
   result[0] = LLVMBuildICmp(builder, LLVMIntNE, result[0], lp_build_const_int_vec(gallivm, bld->int_bld.type, 0), "");
}

static void emit_reduce(struct lp_build_nir_soa_context *bld, LLVMValueRef src,
                        nir_intrinsic_instr *instr, LLVMValueRef result[4])
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;
   uint32_t bit_size = nir_src_bit_size(instr->src[0]);
   /* can't use llvm reduction intrinsics because of exec_mask */
   LLVMValueRef exec_mask = group_op_mask_vec(bld);
   nir_op reduction_op = nir_intrinsic_reduction_op(instr);

   uint32_t cluster_size = 0;

   if (instr->intrinsic == nir_intrinsic_reduce)
      cluster_size = nir_intrinsic_cluster_size(instr);

   if (cluster_size == 0)
      cluster_size = bld->int_bld.type.length;

   if (bit_size == 1) {
      bit_size = 8;
      src = LLVMBuildZExt(builder, src, bld->uint8_bld.vec_type, "");
   }

   LLVMValueRef res_store = NULL;
   LLVMValueRef scan_store;
   struct lp_build_context *int_bld = get_int_bld(bld, true, bit_size, true);

   res_store = lp_build_alloca(gallivm, int_bld->vec_type, "");
   scan_store = lp_build_alloca(gallivm, int_bld->elem_type, "");

   struct lp_build_context elem_bld;
   bool is_flt = reduction_op == nir_op_fadd ||
      reduction_op == nir_op_fmul ||
      reduction_op == nir_op_fmin ||
      reduction_op == nir_op_fmax;
   bool is_unsigned = reduction_op == nir_op_umin ||
      reduction_op == nir_op_umax;

   struct lp_build_context *vec_bld = is_flt ? get_flt_bld(bld, bit_size, true) :
      get_int_bld(bld, is_unsigned, bit_size, true);

   lp_build_context_init(&elem_bld, gallivm, lp_elem_type(vec_bld->type));

   LLVMValueRef store_val = NULL;
   /*
    * Put the identity value for the operation into the storage
    */
   switch (reduction_op) {
   case nir_op_fmin: {
      LLVMValueRef flt_max = bit_size == 64 ? LLVMConstReal(LLVMDoubleTypeInContext(gallivm->context), INFINITY) :
         (bit_size == 16 ? LLVMConstReal(LLVMHalfTypeInContext(gallivm->context), INFINITY) : lp_build_const_float(gallivm, INFINITY));
      store_val = LLVMBuildBitCast(builder, flt_max, int_bld->elem_type, "");
      break;
   }
   case nir_op_fmax: {
      LLVMValueRef flt_min = bit_size == 64 ? LLVMConstReal(LLVMDoubleTypeInContext(gallivm->context), -INFINITY) :
         (bit_size == 16 ? LLVMConstReal(LLVMHalfTypeInContext(gallivm->context), -INFINITY) : lp_build_const_float(gallivm, -INFINITY));
      store_val = LLVMBuildBitCast(builder, flt_min, int_bld->elem_type, "");
      break;
   }
   case nir_op_fmul: {
      LLVMValueRef flt_one = bit_size == 64 ? LLVMConstReal(LLVMDoubleTypeInContext(gallivm->context), 1.0) :
         (bit_size == 16 ? LLVMConstReal(LLVMHalfTypeInContext(gallivm->context), 1.0) : lp_build_const_float(gallivm, 1.0));
      store_val = LLVMBuildBitCast(builder, flt_one, int_bld->elem_type, "");
      break;
   }
   case nir_op_umin:
      switch (bit_size) {
      case 8:
         store_val = LLVMConstInt(LLVMInt8TypeInContext(gallivm->context), UINT8_MAX, 0);
         break;
      case 16:
         store_val = LLVMConstInt(LLVMInt16TypeInContext(gallivm->context), UINT16_MAX, 0);
         break;
      case 32:
      default:
         store_val  = lp_build_const_int32(gallivm, UINT_MAX);
         break;
      case 64:
         store_val  = lp_build_const_int64(gallivm, UINT64_MAX);
         break;
      }
      break;
   case nir_op_imin:
      switch (bit_size) {
      case 8:
         store_val = LLVMConstInt(LLVMInt8TypeInContext(gallivm->context), INT8_MAX, 0);
         break;
      case 16:
         store_val = LLVMConstInt(LLVMInt16TypeInContext(gallivm->context), INT16_MAX, 0);
         break;
      case 32:
      default:
         store_val  = lp_build_const_int32(gallivm, INT_MAX);
         break;
      case 64:
         store_val  = lp_build_const_int64(gallivm, INT64_MAX);
         break;
      }
      break;
   case nir_op_imax:
      switch (bit_size) {
      case 8:
         store_val = LLVMConstInt(LLVMInt8TypeInContext(gallivm->context), INT8_MIN, 0);
         break;
      case 16:
         store_val = LLVMConstInt(LLVMInt16TypeInContext(gallivm->context), INT16_MIN, 0);
         break;
      case 32:
      default:
         store_val  = lp_build_const_int32(gallivm, INT_MIN);
         break;
      case 64:
         store_val  = lp_build_const_int64(gallivm, INT64_MIN);
         break;
      }
      break;
   case nir_op_imul:
      switch (bit_size) {
      case 8:
         store_val = LLVMConstInt(LLVMInt8TypeInContext(gallivm->context), 1, 0);
         break;
      case 16:
         store_val = LLVMConstInt(LLVMInt16TypeInContext(gallivm->context), 1, 0);
         break;
      case 32:
      default:
         store_val  = lp_build_const_int32(gallivm, 1);
         break;
      case 64:
         store_val  = lp_build_const_int64(gallivm, 1);
         break;
      }
      break;
   case nir_op_iand:
      switch (bit_size) {
      case 8:
         store_val = LLVMConstInt(LLVMInt8TypeInContext(gallivm->context), 0xff, 0);
         break;
      case 16:
         store_val = LLVMConstInt(LLVMInt16TypeInContext(gallivm->context), 0xffff, 0);
         break;
      case 32:
      default:
         store_val  = lp_build_const_int32(gallivm, 0xffffffff);
         break;
      case 64:
         store_val  = lp_build_const_int64(gallivm, 0xffffffffffffffffLL);
         break;
      }
      break;
   default:
      break;
   }
   if (store_val)
      LLVMBuildStore(builder, store_val, scan_store);

   LLVMValueRef outer_cond = LLVMBuildICmp(builder, LLVMIntNE, exec_mask, bld->uint_bld.zero, "");

   for (uint32_t i = 0; i < bld->uint_bld.type.length; i++) {
      LLVMValueRef counter = lp_build_const_int32(gallivm, i);

      struct lp_build_if_state ifthen;
      LLVMValueRef if_cond = LLVMBuildExtractElement(gallivm->builder, outer_cond, counter, "");
      lp_build_if(&ifthen, gallivm, if_cond);

      LLVMValueRef value = LLVMBuildExtractElement(gallivm->builder, src, counter, "");

      LLVMValueRef res = NULL;
      LLVMValueRef scan_val = LLVMBuildLoad2(gallivm->builder, int_bld->elem_type, scan_store, "");

      if (instr->intrinsic != nir_intrinsic_reduce)
         res = LLVMBuildLoad2(gallivm->builder, int_bld->vec_type, res_store, "");

      if (instr->intrinsic == nir_intrinsic_exclusive_scan)
         res = LLVMBuildInsertElement(builder, res, scan_val, counter, "");

      if (is_flt) {
         scan_val = LLVMBuildBitCast(builder, scan_val, elem_bld.elem_type, "");
         value = LLVMBuildBitCast(builder, value, elem_bld.elem_type, "");
      }

      switch (reduction_op) {
      case nir_op_fadd:
      case nir_op_iadd:
         scan_val = lp_build_add(&elem_bld, value, scan_val);
         break;
      case nir_op_fmul:
      case nir_op_imul:
         scan_val = lp_build_mul(&elem_bld, value, scan_val);
         break;
      case nir_op_imin:
      case nir_op_umin:
      case nir_op_fmin:
         scan_val = lp_build_min(&elem_bld, value, scan_val);
         break;
      case nir_op_imax:
      case nir_op_umax:
      case nir_op_fmax:
         scan_val = lp_build_max(&elem_bld, value, scan_val);
         break;
      case nir_op_iand:
         scan_val = lp_build_and(&elem_bld, value, scan_val);
         break;
      case nir_op_ior:
         scan_val = lp_build_or(&elem_bld, value, scan_val);
         break;
      case nir_op_ixor:
         scan_val = lp_build_xor(&elem_bld, value, scan_val);
         break;
      default:
         assert(0);
         break;
      }

      if (is_flt)
         scan_val = LLVMBuildBitCast(builder, scan_val, int_bld->elem_type, "");
      LLVMBuildStore(builder, scan_val, scan_store);

      if (instr->intrinsic == nir_intrinsic_inclusive_scan)
         res = LLVMBuildInsertElement(builder, res, scan_val, counter, "");

      if (instr->intrinsic != nir_intrinsic_reduce)
         LLVMBuildStore(builder, res, res_store);

      lp_build_endif(&ifthen);

      if (instr->intrinsic == nir_intrinsic_reduce && (i % cluster_size) == (cluster_size - 1)) {
         res = LLVMBuildLoad2(gallivm->builder, int_bld->vec_type, res_store, "");
         scan_val = LLVMBuildLoad2(gallivm->builder, int_bld->elem_type, scan_store, "");

         if (store_val)
            LLVMBuildStore(builder, store_val, scan_store);
         else
            LLVMBuildStore(builder, LLVMConstNull(int_bld->elem_type), scan_store);

         LLVMValueRef cluster_index = lp_build_const_int32(gallivm, i / cluster_size);
         res = LLVMBuildInsertElement(builder, res, scan_val, cluster_index, "");

         LLVMBuildStore(builder, res, res_store);
      }
   }

   LLVMValueRef res = LLVMBuildLoad2(gallivm->builder, int_bld->vec_type, res_store, "");

   if (instr->intrinsic == nir_intrinsic_reduce) {
      LLVMValueRef swizzle[LP_MAX_VECTOR_LENGTH];
      for (uint32_t i = 0; i < bld->int_bld.type.length; i++)
         swizzle[i] = lp_build_const_int32(gallivm, i / cluster_size);

      LLVMValueRef undef = LLVMGetUndef(int_bld->vec_type);
      result[0] = LLVMBuildShuffleVector(
         builder, res, undef, LLVMConstVector(swizzle, bld->int_bld.type.length), "");
   } else {
      result[0] = res;
   }

   if (instr->def.bit_size == 1)
      result[0] = LLVMBuildICmp(builder, LLVMIntNE, result[0], int_bld->zero, "");
}

static void emit_read_invocation(struct lp_build_nir_soa_context *bld,
                                 LLVMValueRef src,
                                 unsigned bit_size,
                                 LLVMValueRef invoc,
                                 LLVMValueRef result[4])
{
   struct gallivm_state *gallivm = bld->base.gallivm;

   if (!lp_value_is_divergent(src)) {
      result[0] = src;
      return;
   }

   if (invoc && !lp_value_is_divergent(invoc)) {
      result[0] = LLVMBuildExtractElement(gallivm->builder, src, invoc, "");
      return;
   }

   LLVMValueRef idx = first_active_invocation(bld, false);

   /* If we're emitting readInvocation() (as opposed to readFirstInvocation),
    * use the first active channel to pull the invocation index number out of
    * the invocation arg.
    */
   if (invoc)
      idx = LLVMBuildExtractElement(gallivm->builder, invoc, idx, "");

   result[0] = LLVMBuildExtractElement(gallivm->builder, src, idx, "");
}

static void
emit_set_vertex_and_primitive_count(struct lp_build_nir_soa_context *bld,
                                    LLVMValueRef vert_count,
                                    LLVMValueRef prim_count)
{
   bld->mesh_iface->emit_vertex_and_primitive_count(bld->mesh_iface, &bld->base, vert_count, prim_count);
}

static void
emit_launch_mesh_workgroups(struct lp_build_nir_soa_context *bld,
                            LLVMValueRef *launch_grid)
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMTypeRef vec_type = LLVMArrayType(LLVMInt32TypeInContext(gallivm->context), 3);

   LLVMValueRef local_invoc_idx = get_local_invocation_index(bld);

   vec_type = LLVMPointerType(vec_type, 0);

   local_invoc_idx = LLVMBuildExtractElement(gallivm->builder, local_invoc_idx, lp_build_const_int32(gallivm, 0), "");
   LLVMValueRef if_cond = LLVMBuildICmp(gallivm->builder, LLVMIntEQ, local_invoc_idx, lp_build_const_int32(gallivm, 0), "");
   struct lp_build_if_state ifthen;
   lp_build_if(&ifthen, gallivm, if_cond);
   LLVMValueRef ptr = bld->payload_ptr;
   ptr = LLVMBuildPtrToInt(gallivm->builder, ptr, bld->int64_bld.elem_type, "");
   for (unsigned i = 0; i < 3; i++) {
      LLVMValueRef this_ptr = LLVMBuildIntToPtr(gallivm->builder, ptr, LLVMPointerType(LLVMInt32TypeInContext(gallivm->context), 0), "");
      LLVMBuildStore(gallivm->builder, launch_grid[i], this_ptr);
      ptr = LLVMBuildAdd(gallivm->builder, ptr, lp_build_const_int64(gallivm, 4), "");
   }
   lp_build_endif(&ifthen);
}

static void
emit_call(struct lp_build_nir_soa_context *bld,
          struct lp_build_fn *fn,
          int num_args,
          LLVMValueRef *args)
{
   args[0] = mask_vec(bld);
   args[1] = bld->call_context_ptr;
   LLVMBuildCall2(bld->base.gallivm->builder, fn->fn_type, fn->fn, args, num_args, "");
}

static LLVMValueRef get_scratch_thread_offsets(struct gallivm_state *gallivm,
                                               struct lp_type type,
                                               unsigned scratch_size)
{
   LLVMTypeRef elem_type = lp_build_int_elem_type(gallivm, type);
   LLVMValueRef elems[LP_MAX_VECTOR_LENGTH];
   unsigned i;

   if (type.length == 1)
      return LLVMConstInt(elem_type, 0, 0);

   for (i = 0; i < type.length; ++i)
      elems[i] = LLVMConstInt(elem_type, scratch_size * i, 0);

   return LLVMConstVector(elems, type.length);
}

static void
emit_clock(struct lp_build_nir_soa_context *bld,
           LLVMValueRef dst[4])
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;
   struct lp_build_context *uint_bld = get_int_bld(bld, true, 32, false);

   lp_init_clock_hook(gallivm);

   LLVMTypeRef get_time_type = LLVMFunctionType(LLVMInt64TypeInContext(gallivm->context), NULL, 0, 1);
   LLVMValueRef result = LLVMBuildCall2(builder, get_time_type, gallivm->get_time_hook, NULL, 0, "");

   LLVMValueRef hi = LLVMBuildShl(builder, result, lp_build_const_int64(gallivm, 32), "");
   hi = LLVMBuildTrunc(builder, hi, uint_bld->elem_type, "");
   LLVMValueRef lo = LLVMBuildTrunc(builder, result, uint_bld->elem_type, "");
   dst[0] = lo;
   dst[1] = hi;
}

LLVMTypeRef
lp_build_cs_func_call_context(struct gallivm_state *gallivm, int length,
                              LLVMTypeRef context_type, LLVMTypeRef resources_type)
{
   LLVMTypeRef args[LP_NIR_CALL_CONTEXT_MAX_ARGS];

   args[LP_NIR_CALL_CONTEXT_CONTEXT] = LLVMPointerType(context_type, 0);
   args[LP_NIR_CALL_CONTEXT_RESOURCES] = LLVMPointerType(resources_type, 0);
   args[LP_NIR_CALL_CONTEXT_SHARED] = LLVMPointerType(LLVMInt32TypeInContext(gallivm->context), 0); /* shared_ptr */
   args[LP_NIR_CALL_CONTEXT_SCRATCH] = LLVMPointerType(LLVMInt8TypeInContext(gallivm->context), 0); /* scratch ptr */
   args[LP_NIR_CALL_CONTEXT_WORK_DIM] = LLVMInt32TypeInContext(gallivm->context); /* work_dim */
   args[LP_NIR_CALL_CONTEXT_THREAD_ID_0] = LLVMVectorType(LLVMInt32TypeInContext(gallivm->context), length); /* system_values.thread_id[0] */
   args[LP_NIR_CALL_CONTEXT_THREAD_ID_1] = LLVMVectorType(LLVMInt32TypeInContext(gallivm->context), length); /* system_values.thread_id[1] */
   args[LP_NIR_CALL_CONTEXT_THREAD_ID_2] = LLVMVectorType(LLVMInt32TypeInContext(gallivm->context), length); /* system_values.thread_id[2] */
   args[LP_NIR_CALL_CONTEXT_BLOCK_ID_0] = LLVMInt32TypeInContext(gallivm->context); /* system_values.block_id[0] */
   args[LP_NIR_CALL_CONTEXT_BLOCK_ID_1] = LLVMInt32TypeInContext(gallivm->context); /* system_values.block_id[1] */
   args[LP_NIR_CALL_CONTEXT_BLOCK_ID_2] = LLVMInt32TypeInContext(gallivm->context); /* system_values.block_id[2] */

   args[LP_NIR_CALL_CONTEXT_GRID_SIZE_0] = LLVMInt32TypeInContext(gallivm->context); /* system_values.grid_size[0] */
   args[LP_NIR_CALL_CONTEXT_GRID_SIZE_1] = LLVMInt32TypeInContext(gallivm->context); /* system_values.grid_size[1] */
   args[LP_NIR_CALL_CONTEXT_GRID_SIZE_2] = LLVMInt32TypeInContext(gallivm->context); /* system_values.grid_size[2] */
   args[LP_NIR_CALL_CONTEXT_BLOCK_SIZE_0] = LLVMInt32TypeInContext(gallivm->context); /* system_values.block_size[0] */
   args[LP_NIR_CALL_CONTEXT_BLOCK_SIZE_1] = LLVMInt32TypeInContext(gallivm->context); /* system_values.block_size[1] */
   args[LP_NIR_CALL_CONTEXT_BLOCK_SIZE_2] = LLVMInt32TypeInContext(gallivm->context); /* system_values.block_size[2] */

   LLVMTypeRef stype = LLVMStructTypeInContext(gallivm->context, args, LP_NIR_CALL_CONTEXT_MAX_ARGS, 0);
   return stype;
}

static void
build_call_context(struct lp_build_nir_soa_context *bld)
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   bld->call_context_ptr = lp_build_alloca(gallivm, bld->call_context_type, "callcontext");
   LLVMValueRef call_context = LLVMGetUndef(bld->call_context_type);
   call_context = LLVMBuildInsertValue(gallivm->builder,
                                       call_context, bld->context_ptr, LP_NIR_CALL_CONTEXT_CONTEXT, "");
   call_context = LLVMBuildInsertValue(gallivm->builder,
                                       call_context, bld->resources_ptr, LP_NIR_CALL_CONTEXT_RESOURCES, "");
   if (bld->shared_ptr) {
      call_context = LLVMBuildInsertValue(gallivm->builder,
                                          call_context, bld->shared_ptr, LP_NIR_CALL_CONTEXT_SHARED, "");
   } else {
      call_context = LLVMBuildInsertValue(gallivm->builder, call_context,
                                          LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(gallivm->context), 0)),
                                          LP_NIR_CALL_CONTEXT_SHARED, "");
   }
   if (bld->scratch_ptr) {
      call_context = LLVMBuildInsertValue(gallivm->builder,
                                          call_context, bld->scratch_ptr, LP_NIR_CALL_CONTEXT_SCRATCH, "");
   } else {
      call_context = LLVMBuildInsertValue(gallivm->builder, call_context,
                                          LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(gallivm->context), 0)),
                                          LP_NIR_CALL_CONTEXT_SCRATCH, "");
   }
   call_context = LLVMBuildInsertValue(gallivm->builder,
                                       call_context, bld->system_values.work_dim, LP_NIR_CALL_CONTEXT_WORK_DIM, "");
   call_context = LLVMBuildInsertValue(gallivm->builder,
                                       call_context, bld->system_values.thread_id[0], LP_NIR_CALL_CONTEXT_THREAD_ID_0, "");
   call_context = LLVMBuildInsertValue(gallivm->builder,
                                       call_context, bld->system_values.thread_id[1], LP_NIR_CALL_CONTEXT_THREAD_ID_1, "");
   call_context = LLVMBuildInsertValue(gallivm->builder,
                                       call_context, bld->system_values.thread_id[2], LP_NIR_CALL_CONTEXT_THREAD_ID_2, "");
   call_context = LLVMBuildInsertValue(gallivm->builder,
                                       call_context, bld->system_values.block_id[0], LP_NIR_CALL_CONTEXT_BLOCK_ID_0, "");
   call_context = LLVMBuildInsertValue(gallivm->builder,
                                       call_context, bld->system_values.block_id[1], LP_NIR_CALL_CONTEXT_BLOCK_ID_1, "");
   call_context = LLVMBuildInsertValue(gallivm->builder,
                                       call_context, bld->system_values.block_id[2], LP_NIR_CALL_CONTEXT_BLOCK_ID_2, "");
   call_context = LLVMBuildInsertValue(gallivm->builder,
                                       call_context, bld->system_values.grid_size[0], LP_NIR_CALL_CONTEXT_GRID_SIZE_0, "");
   call_context = LLVMBuildInsertValue(gallivm->builder,
                                       call_context, bld->system_values.grid_size[1], LP_NIR_CALL_CONTEXT_GRID_SIZE_1, "");
   call_context = LLVMBuildInsertValue(gallivm->builder,
                                       call_context, bld->system_values.grid_size[2], LP_NIR_CALL_CONTEXT_GRID_SIZE_2, "");
   call_context = LLVMBuildInsertValue(gallivm->builder,
                                       call_context, bld->system_values.block_size[0], LP_NIR_CALL_CONTEXT_BLOCK_SIZE_0, "");
   call_context = LLVMBuildInsertValue(gallivm->builder,
                                       call_context, bld->system_values.block_size[1], LP_NIR_CALL_CONTEXT_BLOCK_SIZE_1, "");
   call_context = LLVMBuildInsertValue(gallivm->builder,
                                       call_context, bld->system_values.block_size[2], LP_NIR_CALL_CONTEXT_BLOCK_SIZE_2, "");
   LLVMBuildStore(gallivm->builder, call_context, bld->call_context_ptr);
}

static void
visit_cf_list(struct lp_build_nir_soa_context *bld,
              struct exec_list *list);

static LLVMValueRef
cast_type(struct lp_build_nir_soa_context *bld, LLVMValueRef val,
          nir_alu_type alu_type, unsigned bit_size)
{
   /* bit_size == 1 means that the value is a boolean which always has the i1 element type. */
   if (bit_size == 1)
      return val;

   bool vector = LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMVectorTypeKind;

   LLVMBuilderRef builder = bld->base.gallivm->builder;
   switch (alu_type) {
   case nir_type_float:
      switch (bit_size) {
      case 16:
         return LLVMBuildBitCast(builder, val, vector ? bld->half_bld.vec_type : bld->half_bld.elem_type, "");
      case 32:
         return LLVMBuildBitCast(builder, val, vector ? bld->base.vec_type : bld->base.elem_type, "");
      case 64:
         return LLVMBuildBitCast(builder, val, vector ? bld->dbl_bld.vec_type : bld->dbl_bld.elem_type, "");
      default:
         assert(0);
         break;
      }
      break;
   case nir_type_int:
      switch (bit_size) {
      case 8:
         return LLVMBuildBitCast(builder, val, vector ? bld->int8_bld.vec_type : bld->int8_bld.elem_type, "");
      case 16:
         return LLVMBuildBitCast(builder, val, vector ? bld->int16_bld.vec_type : bld->int16_bld.elem_type, "");
      case 32:
         return LLVMBuildBitCast(builder, val, vector ? bld->int_bld.vec_type : bld->int_bld.elem_type, "");
      case 64:
         return LLVMBuildBitCast(builder, val, vector ? bld->int64_bld.vec_type : bld->int64_bld.elem_type, "");
      default:
         assert(0);
         break;
      }
      break;
   case nir_type_uint:
      switch (bit_size) {
      case 8:
         return LLVMBuildBitCast(builder, val, vector ? bld->uint8_bld.vec_type : bld->uint8_bld.elem_type, "");
      case 16:
         return LLVMBuildBitCast(builder, val, vector ? bld->uint16_bld.vec_type : bld->uint16_bld.elem_type, "");
      case 32:
         return LLVMBuildBitCast(builder, val, vector ? bld->uint_bld.vec_type : bld->uint_bld.elem_type, "");
      case 64:
         return LLVMBuildBitCast(builder, val, vector ? bld->uint64_bld.vec_type : bld->uint64_bld.elem_type, "");
      default:
         assert(0);
         break;
      }
      break;
   case nir_type_uint32:
      return LLVMBuildBitCast(builder, val, vector ? bld->uint_bld.vec_type : bld->uint_bld.elem_type, "");
   default:
      return val;
   }
   return NULL;
}

static unsigned
glsl_sampler_to_pipe(int sampler_dim, bool is_array)
{
   unsigned pipe_target = PIPE_BUFFER;
   switch (sampler_dim) {
   case GLSL_SAMPLER_DIM_1D:
      pipe_target = is_array ? PIPE_TEXTURE_1D_ARRAY : PIPE_TEXTURE_1D;
      break;
   case GLSL_SAMPLER_DIM_2D:
      pipe_target = is_array ? PIPE_TEXTURE_2D_ARRAY : PIPE_TEXTURE_2D;
      break;
   case GLSL_SAMPLER_DIM_SUBPASS:
   case GLSL_SAMPLER_DIM_SUBPASS_MS:
      pipe_target = PIPE_TEXTURE_2D_ARRAY;
      break;
   case GLSL_SAMPLER_DIM_3D:
      pipe_target = PIPE_TEXTURE_3D;
      break;
   case GLSL_SAMPLER_DIM_MS:
      pipe_target = is_array ? PIPE_TEXTURE_2D_ARRAY : PIPE_TEXTURE_2D;
      break;
   case GLSL_SAMPLER_DIM_CUBE:
      pipe_target = is_array ? PIPE_TEXTURE_CUBE_ARRAY : PIPE_TEXTURE_CUBE;
      break;
   case GLSL_SAMPLER_DIM_RECT:
      pipe_target = PIPE_TEXTURE_RECT;
      break;
   case GLSL_SAMPLER_DIM_BUF:
      pipe_target = PIPE_BUFFER;
      break;
   default:
      break;
   }
   return pipe_target;
}

static uint32_t
get_src_index(nir_src *src)
{
   nir_instr *instr = nir_src_parent_instr(src);
   switch (instr->type) {
   case nir_instr_type_alu: {
      nir_alu_instr *alu = nir_instr_as_alu(instr);
      return ((uintptr_t)src - (uintptr_t)&alu->src[0].src) / sizeof(nir_alu_src);
   }
   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
      return ((uintptr_t)src - (uintptr_t)&intr->src[0]) / sizeof(nir_src);
   }
   case nir_instr_type_tex: {
      nir_tex_instr *intr = nir_instr_as_tex(instr);
      return ((uintptr_t)src - (uintptr_t)&intr->src[0].src) / sizeof(nir_tex_src);
   }
   case nir_instr_type_deref: {
      nir_deref_instr *deref = nir_instr_as_deref(instr);
      return nir_srcs_equal(deref->parent, *src) ? 0 : 1;
   }
   case nir_instr_type_call: {
      nir_call_instr *call = nir_instr_as_call(instr);
      return ((uintptr_t)src - (uintptr_t)&call->params[0]) / sizeof(nir_src);
   }
   default:
      UNREACHABLE("Unhandled instruction type");
   }
}

static void
get_instr_src_vec(struct lp_build_nir_soa_context *bld, nir_instr *instr, uint32_t src_index, LLVMValueRef *src_values)
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;

   nir_src *src = NULL;
   switch (instr->type) {
   case nir_instr_type_alu: {
      nir_alu_instr *alu = nir_instr_as_alu(instr);
      src = &alu->src[src_index].src;
      break;
   }
   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
      src = &intr->src[src_index];
      break;
   }
   case nir_instr_type_tex: {
      nir_tex_instr *tex = nir_instr_as_tex(instr);
      src = &tex->src[src_index].src;
      break;
   }
   case nir_instr_type_deref: {
      nir_deref_instr *deref = nir_instr_as_deref(instr);
      src = src_index == 0 ? &deref->parent : &deref->arr.index;
      break;
   }
   case nir_instr_type_call: {
      nir_call_instr *call = nir_instr_as_call(instr);
      src = &call->params[src_index];
      break;
   }
   default:
      UNREACHABLE("Unhandled instruction type");
   }

   bool divergent = lp_nir_instr_src_divergent(instr, src_index);

   memcpy(src_values,
          &bld->ssa_defs[src->ssa->index * NIR_MAX_VEC_COMPONENTS * 2 + divergent * NIR_MAX_VEC_COMPONENTS],
          sizeof(LLVMValueRef) * nir_src_num_components(*src));

   for (uint32_t c = 0; c < nir_src_num_components(*src); c++) {
      if (src_values[c])
         continue;

      /* The value should only be NULL when using a divergent value as a uniform source.
       * Extracting the first_active_invocation element needs to be done at the consuming
       * instruction because the exec mask can change.
       */
      assert(!divergent);
      src_values[c] = LLVMBuildExtractElement(
         builder, bld->ssa_defs[src->ssa->index * NIR_MAX_VEC_COMPONENTS * 2 + NIR_MAX_VEC_COMPONENTS + c],
         first_active_invocation(bld, true), "");
   }
}

static void
get_src_vec(struct lp_build_nir_soa_context *bld, uint32_t src_index, LLVMValueRef *src_values)
{
   get_instr_src_vec(bld, bld->instr, src_index, src_values);
}

static LLVMValueRef
get_src(struct lp_build_nir_soa_context *bld, nir_src *src, uint32_t component)
{
   if (nir_src_is_if(src))
      return bld->ssa_defs[src->ssa->index * NIR_MAX_VEC_COMPONENTS * 2 + NIR_MAX_VEC_COMPONENTS + component];

   LLVMValueRef result[NIR_MAX_VEC_COMPONENTS] = { NULL };
   get_instr_src_vec(bld, nir_src_parent_instr(src), get_src_index(src), result);
   return result[component];
}

static void
assign_ssa_dest(struct lp_build_nir_soa_context *bld, const nir_def *ssa,
                LLVMValueRef vals[NIR_MAX_VEC_COMPONENTS])
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;

   if (gallivm->di_builder && nir_def_instr(ssa)->has_debug_info) {
      /* Cast away the constness */
      nir_instr_debug_info *debug_info =
         nir_instr_get_debug_info((void *) nir_def_instr(ssa));

      /* Use "ssa_%u" because GDB cannot handle "%%%u" */
      char name[16];
      snprintf(name, sizeof(name), "ssa_%u", ssa->index);

      LLVMTypeRef type = LLVMTypeOf(vals[0]);
      if (ssa->num_components > 1)
         type = LLVMArrayType(type, ssa->num_components);

      LLVMBuilderRef first_builder = lp_create_builder_at_entry(gallivm);
      LLVMValueRef var = LLVMBuildAlloca(first_builder, type, name);
      LLVMBuildStore(first_builder, LLVMConstNull(type), var);
      LLVMDisposeBuilder(first_builder);

      if (ssa->num_components > 1)
         LLVMBuildStore(builder, lp_nir_array_build_gather_values(builder, vals, ssa->num_components), var);
      else
         LLVMBuildStore(builder, vals[0], var);

      LLVMMetadataRef di_type = lp_bld_debug_info_type(gallivm, type);
      LLVMMetadataRef di_var = LLVMDIBuilderCreateAutoVariable(
         gallivm->di_builder, gallivm->di_function, name, strlen(name),
         gallivm->file, debug_info->line, di_type, true, LLVMDIFlagZero, 0);

      LLVMMetadataRef di_expr = LLVMDIBuilderCreateExpression(gallivm->di_builder, NULL, 0);

      LLVMMetadataRef di_loc = LLVMDIBuilderCreateDebugLocation(
         gallivm->context, debug_info->line, debug_info->column, gallivm->di_function, NULL);

#if LLVM_VERSION_MAJOR >= 19
      LLVMDIBuilderInsertDeclareRecordAtEnd(gallivm->di_builder, var, di_var, di_expr, di_loc,
                                            LLVMGetInsertBlock(builder));
#else
      LLVMDIBuilderInsertDeclareAtEnd(gallivm->di_builder, var, di_var, di_expr, di_loc,
                                      LLVMGetInsertBlock(builder));
#endif
   }

   bool used_by_divergent = false;
   nir_foreach_use_including_if(use, ssa) {
      bool use_divergent = nir_src_is_if(use);
      if (!use_divergent)
         use_divergent =  lp_nir_instr_src_divergent(nir_src_parent_instr(use), get_src_index(use));
      used_by_divergent |= use_divergent;
   }

   for (uint32_t c = 0; c < ssa->num_components; c++) {
      char name[64];
      sprintf(name, "ssa_%u", ssa->index);
      LLVMSetValueName(vals[c], name);

      if (lp_value_is_divergent(vals[c])) {
         bld->ssa_defs[ssa->index * NIR_MAX_VEC_COMPONENTS * 2 + NIR_MAX_VEC_COMPONENTS + c] = vals[c];
      } else {
         bld->ssa_defs[ssa->index * NIR_MAX_VEC_COMPONENTS * 2 + c] = vals[c];
         if (used_by_divergent) {
            bld->ssa_defs[ssa->index * NIR_MAX_VEC_COMPONENTS * 2 + NIR_MAX_VEC_COMPONENTS + c] =
               lp_build_broadcast(gallivm, LLVMVectorType(LLVMTypeOf(vals[c]), bld->base.type.length), vals[c]);
         }
      }
   }
}

static LLVMValueRef
lp_build_pack(struct lp_build_context *bld, LLVMValueRef src0,
              LLVMValueRef src1, uint32_t src_bit_size)
{
   struct gallivm_state *gallivm = bld->gallivm;
   LLVMBuilderRef builder = gallivm->builder;

   uint32_t length = bld->type.length;

   if (length == 1) {
      LLVMTypeRef vec_type =
         LLVMVectorType(LLVMIntTypeInContext(gallivm->context, src_bit_size), 1);
      src0 = LLVMBuildBitCast(builder, src0, vec_type, "");
      src1 = LLVMBuildBitCast(builder, src1, vec_type, "");
   }

   LLVMValueRef shuffle[LP_MAX_VECTOR_WIDTH / 32];
   for (unsigned i = 0; i < length; i++) {
#if UTIL_ARCH_LITTLE_ENDIAN
      shuffle[i * 2] = lp_build_const_int32(gallivm, i);
      shuffle[i * 2 + 1] = lp_build_const_int32(gallivm, i + length);
#else
      shuffle[i * 2] = lp_build_const_int32(gallivm, i + length);
      shuffle[i * 2 + 1] = lp_build_const_int32(gallivm, i);
#endif
   }
   return LLVMBuildShuffleVector(builder, src0, src1, LLVMConstVector(shuffle, length * 2), "");
}

static LLVMValueRef
lp_build_unpack(struct lp_build_context *bld, LLVMValueRef value,
                uint32_t src_bit_size, uint32_t dst_bit_size,
                uint32_t component)
{
   struct gallivm_state *gallivm = bld->gallivm;
   LLVMBuilderRef builder = gallivm->builder;

   uint32_t length = bld->type.length;
   uint32_t num_components = src_bit_size / dst_bit_size;

   LLVMTypeRef vec_type =
      LLVMVectorType(LLVMIntTypeInContext(gallivm->context, dst_bit_size), num_components * length);
   value = LLVMBuildBitCast(builder, value, vec_type, "");

   if (length == 1)
      return LLVMBuildExtractElement(builder, value, lp_build_const_int32(gallivm, component), "");

   LLVMValueRef shuffle[LP_MAX_VECTOR_WIDTH / 32];
   for (unsigned i = 0; i < length; i++) {
#if UTIL_ARCH_LITTLE_ENDIAN
      shuffle[i] = lp_build_const_int32(gallivm, (i * num_components) + component);
#else
      shuffle[i] = lp_build_const_int32(gallivm, (i * num_components) + (num_components - component - 1));
#endif
   }
   return LLVMBuildShuffleVector(builder, value, LLVMGetUndef(vec_type),
                                 LLVMConstVector(shuffle, length), "");
}

static LLVMValueRef
get_signed_divisor(struct gallivm_state *gallivm,
                   struct lp_build_context *int_bld,
                   struct lp_build_context *mask_bld,
                   int src_bit_size,
                   LLVMValueRef src, LLVMValueRef divisor)
{
   LLVMBuilderRef builder = gallivm->builder;
   /* However for signed divides SIGFPE can occur if the numerator is INT_MIN
      and divisor is -1. */
   /* set mask if numerator == INT_MIN */
   long long min_val;
   switch (src_bit_size) {
   case 8:
      min_val = INT8_MIN;
      break;
   case 16:
      min_val = INT16_MIN;
      break;
   default:
   case 32:
      min_val = INT_MIN;
      break;
   case 64:
      min_val = INT64_MIN;
      break;
   }
   LLVMValueRef div_mask2 = lp_build_cmp(mask_bld, PIPE_FUNC_EQUAL, src,
                                         lp_build_const_int_vec(gallivm, int_bld->type, min_val));
   /* set another mask if divisor is - 1 */
   LLVMValueRef div_mask3 = lp_build_cmp(mask_bld, PIPE_FUNC_EQUAL, divisor,
                                         lp_build_const_int_vec(gallivm, int_bld->type, -1));
   div_mask2 = LLVMBuildAnd(builder, div_mask2, div_mask3, "");

   divisor = lp_build_select(mask_bld, div_mask2, int_bld->one, divisor);
   return divisor;
}

static LLVMValueRef
do_int_divide(struct lp_build_nir_soa_context *bld,
              bool is_unsigned, unsigned src_bit_size,
              LLVMValueRef src, LLVMValueRef src2)
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;
   bool divergent = lp_value_is_divergent(src) || lp_value_is_divergent(src2);
   struct lp_build_context *int_bld = get_int_bld(bld, is_unsigned, src_bit_size, divergent);
   struct lp_build_context *mask_bld = get_int_bld(bld, true, src_bit_size, divergent);

   /* avoid divide by 0. Converted divisor from 0 to -1 */
   LLVMValueRef div_mask = lp_build_cmp(mask_bld, PIPE_FUNC_EQUAL, src2,
                                        mask_bld->zero);

   LLVMValueRef divisor = LLVMBuildOr(builder, div_mask, src2, "");
   if (!is_unsigned) {
      divisor = get_signed_divisor(gallivm, int_bld, mask_bld,
                                   src_bit_size, src, divisor);
   }
   LLVMValueRef result = lp_build_div(int_bld, src, divisor);

   if (!is_unsigned) {
      LLVMValueRef not_div_mask = LLVMBuildNot(builder, div_mask, "");
      return LLVMBuildAnd(builder, not_div_mask, result, "");
   } else
      /* udiv by zero is guaranteed to return 0xffffffff at least with d3d10
       * may as well do same for idiv */
      return LLVMBuildOr(builder, div_mask, result, "");
}

static LLVMValueRef
do_int_mod(struct lp_build_nir_soa_context *bld,
           bool is_unsigned, bool use_src2_sign, unsigned src_bit_size,
           LLVMValueRef src, LLVMValueRef src2)
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;
   bool divergent = lp_value_is_divergent(src) || lp_value_is_divergent(src2);
   struct lp_build_context *int_bld = get_int_bld(bld, is_unsigned, src_bit_size, divergent);
   struct lp_build_context *mask_bld = get_int_bld(bld, true, src_bit_size, divergent);
   LLVMValueRef div_mask = lp_build_cmp(mask_bld, PIPE_FUNC_EQUAL, src2,
                                        mask_bld->zero);
   LLVMValueRef divisor = LLVMBuildOr(builder,
                                      div_mask,
                                      src2, "");
   if (!is_unsigned) {
      divisor = get_signed_divisor(gallivm, int_bld, mask_bld,
                                   src_bit_size, src, divisor);
   }
   LLVMValueRef rem = lp_build_mod(int_bld, src, divisor);
   rem = LLVMBuildOr(builder, div_mask, rem, "");

   if (use_src2_sign) {
      LLVMValueRef add_src2 = LLVMBuildICmp(builder, LLVMIntNE, rem, int_bld->zero, "");
      LLVMValueRef signs_different = LLVMBuildXor(builder, LLVMBuildICmp(builder, LLVMIntSLT, src, int_bld->zero, ""),
                                                  LLVMBuildICmp(builder, LLVMIntSLT, src2, int_bld->zero, ""), "");
      add_src2 = LLVMBuildAnd(builder, add_src2, signs_different, "");
      rem = LLVMBuildSelect(builder, add_src2, LLVMBuildAdd(builder, rem, src2, ""), rem, "");
   }

   return rem;
}

static LLVMValueRef
do_alu_action(struct lp_build_nir_soa_context *bld,
              const nir_alu_instr *instr,
              unsigned src_bit_size[NIR_MAX_VEC_COMPONENTS],
              LLVMValueRef src[NIR_MAX_VEC_COMPONENTS])
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;
   LLVMValueRef result = NULL;

   struct lp_build_context *float_bld = get_flt_bld(bld, src_bit_size[0], instr->def.divergent);
   struct lp_build_context *int_bld = get_int_bld(bld, false, src_bit_size[0], instr->def.divergent);
   struct lp_build_context *uint_bld = get_int_bld(bld, true, src_bit_size[0], instr->def.divergent);
   struct lp_build_context *dst_float_bld = get_flt_bld(bld, instr->def.bit_size, instr->def.divergent);
   struct lp_build_context *dst_int_bld = get_int_bld(bld, false, instr->def.bit_size, instr->def.divergent);
   struct lp_build_context *dst_uint_bld = get_int_bld(bld, true, instr->def.bit_size, instr->def.divergent);

   switch (instr->op) {
   case nir_op_b2f16:
   case nir_op_b2f32:
   case nir_op_b2f64:
      result = LLVMBuildAnd(builder, LLVMBuildSExt(builder, src[0], dst_uint_bld->vec_type, ""),
         LLVMBuildBitCast(builder, lp_build_const_vec(bld->base.gallivm, dst_float_bld->type, 1.0), dst_uint_bld->vec_type, ""), "");
      result = LLVMBuildBitCast(builder, result, dst_float_bld->vec_type, "");
      break;
   case nir_op_b2b1:
      result = LLVMBuildICmp(builder, LLVMIntNE, src[0], int_bld->zero, "");
      break;
   case nir_op_b2b8:
   case nir_op_b2b16:
   case nir_op_b2b32:
      if (src_bit_size[0] > instr->def.bit_size) {
         result = LLVMBuildTrunc(builder, src[0], dst_uint_bld->vec_type, "");
      } else {
         result = LLVMBuildSExt(builder, src[0], dst_uint_bld->vec_type, "");
      }
      break;
   case nir_op_b2i8:
   case nir_op_b2i16:
   case nir_op_b2i32:
   case nir_op_b2i64:
      result = LLVMBuildZExt(builder, src[0], dst_uint_bld->vec_type, "");
      break;
   case nir_op_bit_count:
      result = lp_build_popcount(int_bld, src[0]);
      if (src_bit_size[0] < 32)
         result = LLVMBuildZExt(builder, result, dst_int_bld->vec_type, "");
      else if (src_bit_size[0] > 32)
         result = LLVMBuildTrunc(builder, result, dst_int_bld->vec_type, "");
      break;
   case nir_op_bitfield_select:
      result = lp_build_xor(uint_bld, src[2], lp_build_and(uint_bld, src[0], lp_build_xor(uint_bld, src[1], src[2])));
      break;
   case nir_op_bitfield_reverse:
      result = lp_build_bitfield_reverse(int_bld, src[0]);
      break;
   case nir_op_f2f16:
   case nir_op_f2f32:
   case nir_op_f2f64:
      /*
       * FIXME: for f2f16 we can get here even without lp_has_fp16(), despite
       * not announcing f16 capability. Should not happen from GL side (glsl has
       * lowering pass for f16 pack/unpack functions if not supported) however
       * from Vulkan side things like quantizeToF16, unpack2x16float use this.
       * Need to fix up the llvm type in this case at least, although on some
       * platforms this will still crash if the llvm backend doesn't handle half
       * type on its own (but works on x86 even without f16c).
       * Alternatively could use lp_build_half_to_float/float_to_half() but need
       * to modify to handle rounding differently (need RNE instead of RTZ here).
       * Ideally would be able to just use llvm half type consistently for f16,
       * but not all backends are ready for that.
       */
      if (src_bit_size[0] > instr->def.bit_size) {
         LLVMTypeRef llvm_dst_type = dst_float_bld->vec_type;
         if (instr->def.bit_size == 16 && !lp_has_fp16()) {
            llvm_dst_type = LLVMHalfTypeInContext(gallivm->context);
            if (dst_float_bld->type.length != 1)
               llvm_dst_type = LLVMVectorType(llvm_dst_type, dst_float_bld->type.length);
         }
         result = LLVMBuildFPTrunc(builder, src[0], llvm_dst_type, "");
         /* don't need a bit cast for f16, caller already has one */
      } else {
         LLVMValueRef src0 = src[0];
         if (src_bit_size[0] == 16 && !lp_has_fp16()) {
            LLVMTypeRef llvm_src_type = LLVMHalfTypeInContext(gallivm->context);
            if (dst_float_bld->type.length != 1)
               llvm_src_type = LLVMVectorType(llvm_src_type, dst_float_bld->type.length);
            src0 = LLVMBuildBitCast(builder, src0, llvm_src_type, "");
         }
         result = LLVMBuildFPExt(builder, src0, dst_float_bld->vec_type, "");
      }
      break;
   case nir_op_f2i8:
   case nir_op_f2i16:
   case nir_op_f2i32:
   case nir_op_f2i64:
   case nir_op_f2u8:
   case nir_op_f2u16:
   case nir_op_f2u32:
   case nir_op_f2u64: {
      nir_alu_type dst_type = nir_op_infos[instr->op].output_type;
      bool is_unsigned = nir_alu_type_get_base_type(dst_type) == nir_type_uint;
      LLVMTypeRef int_type = (is_unsigned ? dst_uint_bld : dst_int_bld)->vec_type;

      char name[64];
      char tmp[64];
      char intrinsic[64];
      snprintf(name, sizeof(name), "llvm.fpto%ci.sat", is_unsigned ? 'u' : 's');
      lp_format_intrinsic(tmp, 64, name, int_type);
      lp_format_intrinsic(intrinsic, 64, tmp, LLVMTypeOf(src[0]));
      result = lp_build_intrinsic_unary(builder, intrinsic, int_type, src[0]);
      break;
   }
   case nir_op_fabs:
      result = lp_build_abs(float_bld, src[0]);
      break;
   case nir_op_fadd:
      result = lp_build_add(float_bld, src[0], src[1]);
      break;
   case nir_op_fceil:
      result = lp_build_ceil(float_bld, src[0]);
      break;
   case nir_op_fcos:
      result = lp_build_cos(float_bld, src[0]);
      break;
   case nir_op_fdiv:
      result = lp_build_div(float_bld, src[0], src[1]);
      break;
   case nir_op_feq:
      result = LLVMBuildFCmp(builder, LLVMRealOEQ, src[0], src[1], "");
      break;
   case nir_op_fge:
      result = LLVMBuildFCmp(builder, LLVMRealOGE, src[0], src[1], "");
      break;
   case nir_op_flt:
      result = LLVMBuildFCmp(builder, LLVMRealOLT, src[0], src[1], "");
      break;
   case nir_op_fneu:
      result = LLVMBuildFCmp(builder, LLVMRealUNE, src[0], src[1], "");
      break;
   case nir_op_fexp2:
      result = lp_build_exp2(float_bld, src[0]);
      break;
   case nir_op_ffloor:
      result = lp_build_floor(float_bld, src[0]);
      break;
   case nir_op_ffma:
      result = lp_build_fmuladd(builder, src[0], src[1], src[2]);
      break;
   case nir_op_ffract: {
      LLVMValueRef tmp = lp_build_floor(float_bld, src[0]);
      result = lp_build_sub(float_bld, src[0], tmp);
      break;
   }
   case nir_op_find_lsb: {
      result = lp_build_cttz(int_bld, src[0]);
      if (src_bit_size[0] < 32)
         result = LLVMBuildZExt(builder, result, dst_uint_bld->vec_type, "");
      else if (src_bit_size[0] > 32)
         result = LLVMBuildTrunc(builder, result, dst_uint_bld->vec_type, "");
      break;
   }
   case nir_op_fisfinite32:
      UNREACHABLE("Should have been lowered in nir_opt_algebraic_late.");
   case nir_op_flog2:
      result = lp_build_log2_safe(float_bld, src[0]);
      break;
   case nir_op_fmax:
   case nir_op_fmin: {
      enum gallivm_nan_behavior minmax_nan;
      int first = 0;

      /* If one of the sources is known to be a number (i.e., not NaN), then
       * better code can be generated by passing that information along.
       */
      nir_search_state search;
      search.range_ht = bld->range_ht;
      if (is_a_number(&search, instr, 1,
                      0 /* unused num_components */,
                      NULL /* unused swizzle */)) {
         minmax_nan = GALLIVM_NAN_RETURN_OTHER_SECOND_NONNAN;
      } else if (is_a_number(&search, instr, 0,
                             0 /* unused num_components */,
                             NULL /* unused swizzle */)) {
         first = 1;
         minmax_nan = GALLIVM_NAN_RETURN_OTHER_SECOND_NONNAN;
      } else {
         minmax_nan = GALLIVM_NAN_RETURN_OTHER;
      }

      if (instr->op == nir_op_fmin) {
         result = lp_build_min_ext(float_bld, src[first], src[1 - first], minmax_nan);
      } else {
         result = lp_build_max_ext(float_bld, src[first], src[1 - first], minmax_nan);
      }
      break;
   }
   case nir_op_fmod: {
      result = lp_build_div(float_bld, src[0], src[1]);
      result = lp_build_floor(float_bld, result);
      result = lp_build_mul(float_bld, src[1], result);
      result = lp_build_sub(float_bld, src[0], result);
      break;
   }
   case nir_op_fmul:
      result = lp_build_mul(float_bld, src[0], src[1]);
      break;
   case nir_op_fneg:
      result = lp_build_negate(float_bld, src[0]);
      break;
   case nir_op_fpow:
      result = lp_build_pow(float_bld, src[0], src[1]);
      break;
   case nir_op_frcp:
      result = lp_build_rcp(float_bld, src[0]);
      break;
   case nir_op_fround_even:
      result = lp_build_round(float_bld, src[0]);
      break;
   case nir_op_frsq:
      result = lp_build_rsqrt(float_bld, src[0]);
      break;
   case nir_op_fsat:
      result = lp_build_clamp_zero_one_nanzero(float_bld, src[0]);
      break;
   case nir_op_fsign:
      result = lp_build_sgn(float_bld, src[0]);
      break;
   case nir_op_fsin:
      result = lp_build_sin(float_bld, src[0]);
      break;
   case nir_op_fsqrt:
      result = lp_build_sqrt(float_bld, src[0]);
      break;
   case nir_op_ftrunc:
      result = lp_build_trunc(float_bld, src[0]);
      break;
   case nir_op_i2f16:
   case nir_op_i2f32:
   case nir_op_i2f64:
      result = LLVMBuildSIToFP(builder, src[0],
                               dst_float_bld->vec_type, "");
      break;
   case nir_op_i2i8:
   case nir_op_i2i16:
   case nir_op_i2i32:
   case nir_op_i2i64:
      if (src_bit_size[0] < instr->def.bit_size)
         result = LLVMBuildSExt(builder, src[0], dst_int_bld->vec_type, "");
      else
         result = LLVMBuildTrunc(builder, src[0], dst_int_bld->vec_type, "");
      break;
   case nir_op_iabs:
      result = lp_build_abs(int_bld, src[0]);
      break;
   case nir_op_iadd:
      result = lp_build_add(int_bld, src[0], src[1]);
      break;
   case nir_op_iand:
      result = lp_build_and(int_bld, src[0], src[1]);
      break;
   case nir_op_idiv:
      result = do_int_divide(bld, false, src_bit_size[0], src[0], src[1]);
      break;
   case nir_op_ieq:
      result = LLVMBuildICmp(builder, LLVMIntEQ, src[0], src[1], "");
      break;
   case nir_op_ige:
      result = LLVMBuildICmp(builder, LLVMIntSGE, src[0], src[1], "");
      break;
   case nir_op_ilt:
      result = LLVMBuildICmp(builder, LLVMIntSLT, src[0], src[1], "");
      break;
   case nir_op_ine:
      result = LLVMBuildICmp(builder, LLVMIntNE, src[0], src[1], "");
      break;
   case nir_op_uge:
      result = LLVMBuildICmp(builder, LLVMIntUGE, src[0], src[1], "");
      break;
   case nir_op_ult:
      result = LLVMBuildICmp(builder, LLVMIntULT, src[0], src[1], "");
      break;
   case nir_op_imax:
      result = lp_build_max(int_bld, src[0], src[1]);
      break;
   case nir_op_imin:
      result = lp_build_min(int_bld, src[0], src[1]);
      break;
   case nir_op_imul:
   case nir_op_imul24:
      result = lp_build_mul(int_bld, src[0], src[1]);
      break;
   case nir_op_imul_high: {
      LLVMValueRef hi_bits;
      lp_build_mul_32_lohi(int_bld, src[0], src[1], &hi_bits);
      result = hi_bits;
      break;
   }
   case nir_op_ineg:
      result = lp_build_negate(int_bld, src[0]);
      break;
   case nir_op_inot:
      result = lp_build_not(int_bld, src[0]);
      break;
   case nir_op_ior:
      result = lp_build_or(int_bld, src[0], src[1]);
      break;
   case nir_op_imod:
   case nir_op_irem:
      result = do_int_mod(bld, false, instr->op == nir_op_imod, src_bit_size[0], src[0], src[1]);
      break;
   case nir_op_ishl: {
      if (src_bit_size[0] == 64)
         src[1] = LLVMBuildZExt(builder, src[1], uint_bld->vec_type, "");
      if (src_bit_size[0] < 32)
         src[1] = LLVMBuildTrunc(builder, src[1], uint_bld->vec_type, "");
      src[1] = lp_build_and(uint_bld, src[1], lp_build_const_int_vec(gallivm, uint_bld->type, (src_bit_size[0] - 1)));
      result = lp_build_shl(int_bld, src[0], src[1]);
      break;
   }
   case nir_op_ishr: {
      if (src_bit_size[0] == 64)
         src[1] = LLVMBuildZExt(builder, src[1], uint_bld->vec_type, "");
      if (src_bit_size[0] < 32)
         src[1] = LLVMBuildTrunc(builder, src[1], uint_bld->vec_type, "");
      src[1] = lp_build_and(uint_bld, src[1], lp_build_const_int_vec(gallivm, uint_bld->type, (src_bit_size[0] - 1)));
      result = lp_build_shr(int_bld, src[0], src[1]);
      break;
   }
   case nir_op_isign:
      result = lp_build_sgn(int_bld, src[0]);
      break;
   case nir_op_isub:
      result = lp_build_sub(int_bld, src[0], src[1]);
      break;
   case nir_op_ixor:
      result = lp_build_xor(int_bld, src[0], src[1]);
      break;
   case nir_op_mov:
      result = src[0];
      break;
   case nir_op_unpack_64_2x32_split_x:
      result = lp_build_unpack(uint_bld, src[0], 64, 32, 0);
      break;
   case nir_op_unpack_64_2x32_split_y:
      result = lp_build_unpack(uint_bld, src[0], 64, 32, 1);
      break;

   case nir_op_pack_32_2x16_split: {
      LLVMValueRef tmp = lp_build_pack(uint_bld, src[0], src[1], 16);
      result = LLVMBuildBitCast(builder, tmp, dst_uint_bld->vec_type, "");
      break;
   }
   case nir_op_unpack_32_2x16_split_x:
      result = lp_build_unpack(uint_bld, src[0], 32, 16, 0);
      break;
   case nir_op_unpack_32_2x16_split_y:
      result = lp_build_unpack(uint_bld, src[0], 32, 16, 1);
      break;
   case nir_op_pack_64_2x32_split: {
      LLVMValueRef tmp = lp_build_pack(uint_bld, src[0], src[1], 32);
      result = LLVMBuildBitCast(builder, tmp, dst_uint_bld->vec_type, "");
      break;
   }
   case nir_op_pack_32_4x8_split: {
      LLVMValueRef tmp1 = lp_build_pack(uint_bld, src[0], src[1], 16);
      LLVMValueRef tmp2 = lp_build_pack(uint_bld, src[2], src[3], 16);
      LLVMTypeRef tmp_type = instr->def.divergent ? bld->uint16_bld.vec_type : bld->scalar_uint16_bld.vec_type;
      tmp1 = LLVMBuildBitCast(builder, tmp1, tmp_type, "");
      tmp2 = LLVMBuildBitCast(builder, tmp2, tmp_type, "");
      LLVMValueRef tmp = lp_build_pack(uint_bld, tmp1, tmp2, 16);
      result = LLVMBuildBitCast(builder, tmp, dst_uint_bld->vec_type, "");
      break;
   }
   case nir_op_u2f16:
   case nir_op_u2f32:
   case nir_op_u2f64:
      result = LLVMBuildUIToFP(builder, src[0],
                               dst_float_bld->vec_type, "");
      break;
   case nir_op_u2u8:
   case nir_op_u2u16:
   case nir_op_u2u32:
   case nir_op_u2u64:
      if (src_bit_size[0] < instr->def.bit_size)
         result = LLVMBuildZExt(builder, src[0], dst_uint_bld->vec_type, "");
      else
         result = LLVMBuildTrunc(builder, src[0], dst_uint_bld->vec_type, "");
      break;
   case nir_op_udiv:
      result = do_int_divide(bld, true, src_bit_size[0], src[0], src[1]);
      break;
   case nir_op_ufind_msb: {
      result = lp_build_ctlz(uint_bld, src[0]);
      result = lp_build_sub(uint_bld, lp_build_const_int_vec(gallivm, uint_bld->type, src_bit_size[0] - 1), result);
      if (src_bit_size[0] < 32)
         result = LLVMBuildZExt(builder, result, dst_uint_bld->vec_type, "");
      else
         result = LLVMBuildTrunc(builder, result, dst_uint_bld->vec_type, "");
      break;
   }
   case nir_op_umax:
      result = lp_build_max(uint_bld, src[0], src[1]);
      break;
   case nir_op_umin:
      result = lp_build_min(uint_bld, src[0], src[1]);
      break;
   case nir_op_umod:
      result = do_int_mod(bld, true, false, src_bit_size[0], src[0], src[1]);
      break;
   case nir_op_umul_high: {
      LLVMValueRef hi_bits;
      lp_build_mul_32_lohi(uint_bld, src[0], src[1], &hi_bits);
      result = hi_bits;
      break;
   }
   case nir_op_ushr: {
      if (src_bit_size[0] == 64)
         src[1] = LLVMBuildZExt(builder, src[1], uint_bld->vec_type, "");
      if (src_bit_size[0] < 32)
         src[1] = LLVMBuildTrunc(builder, src[1], uint_bld->vec_type, "");
      src[1] = lp_build_and(uint_bld, src[1], lp_build_const_int_vec(gallivm, uint_bld->type, (src_bit_size[0] - 1)));
      result = lp_build_shr(uint_bld, src[0], src[1]);
      break;
   }
   case nir_op_bcsel: {
      LLVMTypeRef src1_type = LLVMTypeOf(src[1]);
      LLVMTypeRef src2_type = LLVMTypeOf(src[2]);

      if (LLVMGetTypeKind(src1_type) == LLVMPointerTypeKind &&
          LLVMGetTypeKind(src2_type) != LLVMPointerTypeKind) {
         src[2] = LLVMBuildIntToPtr(builder, src[2], src1_type, "");
      } else if (LLVMGetTypeKind(src2_type) == LLVMPointerTypeKind &&
                 LLVMGetTypeKind(src1_type) != LLVMPointerTypeKind) {
         src[1] = LLVMBuildIntToPtr(builder, src[1], src2_type, "");
      }

      for (int i = 1; i <= 2; i++) {
         LLVMTypeRef type = LLVMTypeOf(src[i]);
         if (LLVMGetTypeKind(type) == LLVMPointerTypeKind)
            break;
         src[i] = LLVMBuildBitCast(builder, src[i], get_int_bld(bld, true, src_bit_size[i], instr->def.divergent)->vec_type, "");
      }
      return LLVMBuildSelect(builder, src[0], src[1], src[2], "");
   }
   default:
      assert(0);
      break;
   }
   return result;
}

static void
visit_alu(struct lp_build_nir_soa_context *bld,
          nir_alu_instr *instr)
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMValueRef src[NIR_MAX_VEC_COMPONENTS];
   unsigned src_bit_size[NIR_MAX_VEC_COMPONENTS];
   const unsigned num_components = instr->def.num_components;

   struct lp_type half_type = bld->half_bld.type;
   struct lp_type scalar_half_type = bld->scalar_half_bld.type;
   struct lp_type float_type = bld->base.type;
   struct lp_type scalar_float_type = bld->scalar_base.type;
   struct lp_type double_type = bld->dbl_bld.type;
   struct lp_type scalar_double_type = bld->scalar_dbl_bld.type;

   /* Set the per-intruction float controls. */
   bld->half_bld.type.signed_zero_preserve |=
      !!(instr->fp_fast_math & FLOAT_CONTROLS_SIGNED_ZERO_PRESERVE_FP16);
   bld->scalar_half_bld.type.signed_zero_preserve |=
      !!(instr->fp_fast_math & FLOAT_CONTROLS_SIGNED_ZERO_PRESERVE_FP16);
   bld->half_bld.type.nan_preserve |=
      !!(instr->fp_fast_math & FLOAT_CONTROLS_NAN_PRESERVE_FP16);
   bld->scalar_half_bld.type.nan_preserve |=
      !!(instr->fp_fast_math & FLOAT_CONTROLS_NAN_PRESERVE_FP16);

   bld->base.type.signed_zero_preserve |=
      !!(instr->fp_fast_math & FLOAT_CONTROLS_SIGNED_ZERO_PRESERVE_FP32);
   bld->scalar_base.type.signed_zero_preserve |=
      !!(instr->fp_fast_math & FLOAT_CONTROLS_SIGNED_ZERO_PRESERVE_FP32);
   bld->base.type.nan_preserve |=
      !!(instr->fp_fast_math & FLOAT_CONTROLS_NAN_PRESERVE_FP32);
   bld->scalar_base.type.nan_preserve |=
      !!(instr->fp_fast_math & FLOAT_CONTROLS_NAN_PRESERVE_FP32);

   bld->dbl_bld.type.signed_zero_preserve |=
      !!(instr->fp_fast_math & FLOAT_CONTROLS_SIGNED_ZERO_PRESERVE_FP64);
   bld->scalar_dbl_bld.type.signed_zero_preserve |=
      !!(instr->fp_fast_math & FLOAT_CONTROLS_SIGNED_ZERO_PRESERVE_FP64);
   bld->dbl_bld.type.nan_preserve |=
      !!(instr->fp_fast_math & FLOAT_CONTROLS_NAN_PRESERVE_FP64);
   bld->scalar_dbl_bld.type.nan_preserve |=
      !!(instr->fp_fast_math & FLOAT_CONTROLS_NAN_PRESERVE_FP64);

   for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
      /**
       * Get a source register value for an ALU instruction.
       * This is where swizzles are handled.  There should be no negation
       * or absolute value modifiers.  ALU instructions are expected to be
       * scalar.
       */
      src[i] = get_src(bld, &instr->src[i].src, instr->src[i].swizzle[0]);
      src_bit_size[i] = nir_src_bit_size(instr->src[i].src);
   }

   LLVMValueRef result[NIR_MAX_VEC_COMPONENTS];
   if (instr->op == nir_op_vec4 ||
       instr->op == nir_op_vec3 ||
       instr->op == nir_op_vec2 ||
       instr->op == nir_op_vec8 ||
       instr->op == nir_op_vec16) {
      for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
         result[i] = cast_type(bld, src[i],
                               nir_op_infos[instr->op].input_types[i],
                               src_bit_size[i]);
      }
   } else {
      /* Loop for R,G,B,A channels */
      for (unsigned c = 0; c < num_components; c++) {
         LLVMValueRef src_chan[NIR_MAX_VEC_COMPONENTS];

         /* Loop over instruction operands */
         for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
            if (num_components > 1) {
               src_chan[i] = LLVMBuildExtractValue(gallivm->builder,
                                                     src[i], c, "");
            } else {
               src_chan[i] = src[i];
            }
            src_chan[i] = cast_type(bld, src_chan[i],
                                    nir_op_infos[instr->op].input_types[i],
                                    src_bit_size[i]);
         }
         result[c] = do_alu_action(bld, instr, src_bit_size, src_chan);
         result[c] = cast_type(bld, result[c],
                               nir_op_infos[instr->op].output_type,
                               instr->def.bit_size);
      }
   }
   assign_ssa_dest(bld, &instr->def, result);

   /* Restore the global float controls. */
   bld->half_bld.type = half_type;
   bld->scalar_half_bld.type = scalar_half_type;
   bld->base.type = float_type;
   bld->scalar_base.type = scalar_float_type;
   bld->dbl_bld.type = double_type;
   bld->scalar_dbl_bld.type = scalar_double_type;
}

static void
visit_load_const(struct lp_build_nir_soa_context *bld,
                 const nir_load_const_instr *instr)
{
   LLVMValueRef result[NIR_MAX_VEC_COMPONENTS];
   emit_load_const(bld, instr, result);
   assign_ssa_dest(bld, &instr->def, result);
}

static void
get_deref_offset(struct lp_build_nir_soa_context *bld, nir_deref_instr *instr,
                 bool vs_in, unsigned *vertex_index_out,
                 LLVMValueRef *vertex_index_ref,
                 unsigned *const_out, LLVMValueRef *indir_out)
{
   LLVMBuilderRef builder = bld->base.gallivm->builder;
   nir_variable *var = nir_deref_instr_get_variable(instr);
   nir_deref_path path;
   unsigned idx_lvl = 1;

   nir_deref_path_init(&path, instr, NULL);

   if (vertex_index_out != NULL || vertex_index_ref != NULL) {
      if (vertex_index_ref) {
         *vertex_index_ref = get_src(bld, &path.path[idx_lvl]->arr.index, 0);
         if (vertex_index_out)
            *vertex_index_out = 0;
      } else {
         *vertex_index_out = nir_src_as_uint(path.path[idx_lvl]->arr.index);
      }
      ++idx_lvl;
   }

   uint32_t const_offset = 0;
   LLVMValueRef offset = NULL;

   if (var->data.compact && nir_src_is_const(instr->arr.index)) {
      assert(instr->deref_type == nir_deref_type_array);
      const_offset = nir_src_as_uint(instr->arr.index);
      goto out;
   }

   for (; path.path[idx_lvl]; ++idx_lvl) {
      const struct glsl_type *parent_type = path.path[idx_lvl - 1]->type;
      if (path.path[idx_lvl]->deref_type == nir_deref_type_struct) {
         unsigned index = path.path[idx_lvl]->strct.index;

         for (unsigned i = 0; i < index; i++) {
            const struct glsl_type *ft = glsl_get_struct_field(parent_type, i);
            const_offset += glsl_count_attribute_slots(ft, vs_in);
         }
      } else if (path.path[idx_lvl]->deref_type == nir_deref_type_array) {
         unsigned size = glsl_count_attribute_slots(path.path[idx_lvl]->type, vs_in);
         if (nir_src_is_const(path.path[idx_lvl]->arr.index)) {
            const_offset += nir_src_comp_as_int(path.path[idx_lvl]->arr.index, 0) * size;
         } else {
            LLVMValueRef idx_src = get_src(bld, &path.path[idx_lvl]->arr.index, 0);
            idx_src = cast_type(bld, idx_src, nir_type_uint, 32);
            LLVMValueRef array_off = lp_build_mul(&bld->uint_bld, lp_build_const_int_vec(bld->base.gallivm, bld->base.type, size),
                                                  idx_src);
            if (offset)
               offset = lp_build_add(&bld->uint_bld, offset, array_off);
            else
               offset = array_off;
         }
      } else
         UNREACHABLE("Uhandled deref type in get_deref_instr_offset");
   }

out:
   nir_deref_path_finish(&path);

   if (const_offset && offset)
      offset = LLVMBuildAdd(builder, offset,
                            lp_build_const_int_vec(bld->base.gallivm, bld->uint_bld.type, const_offset),
                            "");
   *const_out = const_offset;
   *indir_out = offset;
}

static void
visit_load_input(struct lp_build_nir_soa_context *bld,
                 nir_intrinsic_instr *instr,
                 LLVMValueRef result[NIR_MAX_VEC_COMPONENTS])
{
   nir_variable var = {0};
   var.data.location = nir_intrinsic_io_semantics(instr).location;
   var.data.driver_location = nir_intrinsic_base(instr);
   var.data.location_frac = nir_intrinsic_component(instr);

   unsigned nc = instr->def.num_components;
   unsigned bit_size = instr->def.bit_size;

   nir_src *offset = nir_get_io_offset_src(instr);
   bool indirect = !nir_src_is_const(*offset);
   if (!indirect)
      assert(nir_src_as_uint(*offset) == 0);
   LLVMValueRef indir_index = indirect ? get_src(bld, offset, 0) : NULL;

   emit_load_var(bld, nir_var_shader_in, nc, bit_size, &var, 0, NULL, 0, indir_index, result);
}

static void
visit_store_output(struct lp_build_nir_soa_context *bld,
                   nir_intrinsic_instr *instr)
{
   nir_variable var = {0};
   var.data.location = nir_intrinsic_io_semantics(instr).location;
   var.data.driver_location = nir_intrinsic_base(instr);
   var.data.location_frac = nir_intrinsic_component(instr);

   unsigned mask = nir_intrinsic_write_mask(instr);

   unsigned bit_size = nir_src_bit_size(instr->src[0]);
   LLVMValueRef src[NIR_MAX_VEC_COMPONENTS] = { NULL };
   get_src_vec(bld, 0, src);

   nir_src *offset = nir_get_io_offset_src(instr);
   bool indirect = !nir_src_is_const(*offset);
   if (!indirect)
      assert(nir_src_as_uint(*offset) == 0);
   LLVMValueRef indir_index = indirect ? get_src(bld, offset, 0) : NULL;

   emit_store_var(bld, nir_var_shader_out, util_last_bit(mask),
                  bit_size, &var, mask, NULL, 0, indir_index, src);
}

static void
visit_load_reg(struct lp_build_nir_soa_context *bld,
               nir_intrinsic_instr *instr,
               LLVMValueRef result[NIR_MAX_VEC_COMPONENTS])
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;

   nir_intrinsic_instr *decl = nir_reg_get_decl(instr->src[0].ssa);
   unsigned base = nir_intrinsic_base(instr);

   struct hash_entry *entry = _mesa_hash_table_search(bld->regs, decl);
   LLVMValueRef reg_storage = (LLVMValueRef)entry->data;

   unsigned bit_size = MAX2(nir_intrinsic_bit_size(decl), 8);
   struct lp_build_context *reg_bld = get_int_bld(bld, true, bit_size, true);

   LLVMValueRef indir_src = NULL;
   if (instr->intrinsic == nir_intrinsic_load_reg_indirect) {
      indir_src = cast_type(bld, get_src(bld, &instr->src[1], 0),
                            nir_type_uint, 32);
   }

   int nc = nir_intrinsic_num_components(decl);
   int num_array_elems = nir_intrinsic_num_array_elems(decl);
   struct lp_build_context *uint_bld = &bld->uint_bld;
   if (indir_src != NULL) {
      LLVMValueRef indirect_val = lp_build_const_int_vec(gallivm, uint_bld->type, base);
      LLVMValueRef max_index = lp_build_const_int_vec(gallivm, uint_bld->type, num_array_elems - 1);
      indirect_val = LLVMBuildAdd(builder, indirect_val, indir_src, "");
      indirect_val = lp_build_min(uint_bld, indirect_val, max_index);
      reg_storage = LLVMBuildBitCast(builder, reg_storage, LLVMPointerType(LLVMInt8TypeInContext(gallivm->context), 0), "");
      for (unsigned i = 0; i < nc; i++) {
         LLVMValueRef indirect_offset = get_soa_array_offsets(uint_bld, indirect_val, nc, i, true);
         indirect_offset = LLVMBuildMul(
            builder, indirect_offset,
            lp_build_const_int_vec(gallivm, uint_bld->type, reg_bld->type.width / 8), "indirect_offset");
         result[i] = lp_build_gather(
            gallivm, reg_bld->type.length, reg_bld->type.width, lp_elem_type(reg_bld->type),
            true, reg_storage, indirect_offset, false);
      }
   } else {
      for (unsigned i = 0; i < nc; i++) {
         result[i] = LLVMBuildLoad2(builder, reg_bld->vec_type,
                                    reg_chan_pointer(bld, reg_bld, decl, reg_storage,
                                                     base, i), "");
      }
   }

   if (instr->def.bit_size == 1) {
      for (uint32_t i = 0; i < nc; i++)
         result[i] = LLVMBuildICmp(builder, LLVMIntNE, result[i], reg_bld->zero, "");
   }
}

static void
visit_store_reg(struct lp_build_nir_soa_context *bld,
                nir_intrinsic_instr *instr)
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;

   nir_intrinsic_instr *decl = nir_reg_get_decl(instr->src[1].ssa);
   unsigned base = nir_intrinsic_base(instr);
   unsigned writemask = nir_intrinsic_write_mask(instr);
   assert(writemask != 0x0);

   LLVMValueRef vals[NIR_MAX_VEC_COMPONENTS] = { NULL };
   get_src_vec(bld, 0, vals);

   struct hash_entry *entry = _mesa_hash_table_search(bld->regs, decl);
   LLVMValueRef reg_storage = (LLVMValueRef)entry->data;

   unsigned bit_size = MAX2(nir_intrinsic_bit_size(decl), 8);
   struct lp_build_context *reg_bld = get_int_bld(bld, true, bit_size, true);

   LLVMValueRef indir_src = NULL;
   if (instr->intrinsic == nir_intrinsic_store_reg_indirect) {
      indir_src = cast_type(bld, get_src(bld, &instr->src[2], 0),
                            nir_type_uint, 32);
   }

   int nc = nir_intrinsic_num_components(decl);

   LLVMValueRef tmp_values[NIR_MAX_VEC_COMPONENTS];
   memcpy(tmp_values, vals, nc * sizeof(LLVMValueRef));

   if (nir_src_bit_size(instr->src[0]) == 1) {
      for (uint32_t i = 0; i < nc; i++)
         tmp_values[i] = LLVMBuildZExt(builder, tmp_values[i], reg_bld->vec_type, "");
   }

   struct lp_build_context *uint_bld = &bld->uint_bld;
   int num_array_elems = nir_intrinsic_num_array_elems(decl);
   if (indir_src != NULL) {
      LLVMValueRef indirect_val = lp_build_const_int_vec(gallivm, uint_bld->type, base);
      LLVMValueRef max_index = lp_build_const_int_vec(gallivm, uint_bld->type, num_array_elems - 1);
      indirect_val = LLVMBuildAdd(builder, indirect_val, indir_src, "");
      indirect_val = lp_build_min(uint_bld, indirect_val, max_index);
      reg_storage = LLVMBuildBitCast(builder, reg_storage, LLVMPointerType(reg_bld->elem_type, 0), "");
      for (unsigned i = 0; i < nc; i++) {
         if (!(writemask & (1 << i)))
            continue;
         LLVMValueRef indirect_offset = get_soa_array_offsets(uint_bld, indirect_val, nc, i, true);
         tmp_values[i] = LLVMBuildBitCast(builder, tmp_values[i], reg_bld->vec_type, "");
         emit_mask_scatter(bld, reg_storage, indirect_offset, tmp_values[i], &bld->exec_mask);
      }
      return;
   }

   for (unsigned i = 0; i < nc; i++) {
      if (!(writemask & (1 << i)))
         continue;
      tmp_values[i] = LLVMBuildBitCast(builder, tmp_values[i], reg_bld->vec_type, "");
      lp_exec_mask_store(&bld->exec_mask, reg_bld, tmp_values[i],
                         reg_chan_pointer(bld, reg_bld, decl, reg_storage,
                                          base, i));
   }
}

static bool
compact_array_index_oob(struct lp_build_nir_soa_context *bld, nir_variable *var, const uint32_t index)
{
   const struct glsl_type *type = var->type;
   if (nir_is_arrayed_io(var, bld->shader->info.stage)) {
      assert(glsl_type_is_array(type));
      type = glsl_get_array_element(type);
   }
   return index >= glsl_get_length(type);
}

static void
visit_load_var(struct lp_build_nir_soa_context *bld,
               nir_intrinsic_instr *instr,
               LLVMValueRef result[NIR_MAX_VEC_COMPONENTS])
{
   nir_deref_instr *deref = nir_def_as_deref(instr->src[0].ssa);
   nir_variable *var = nir_deref_instr_get_variable(deref);
   assert(util_bitcount(deref->modes) == 1);
   nir_variable_mode mode = deref->modes;
   unsigned const_index = 0;
   LLVMValueRef indir_index = NULL;
   LLVMValueRef indir_vertex_index = NULL;
   unsigned vertex_index = 0;
   unsigned nc = instr->def.num_components;
   unsigned bit_size = instr->def.bit_size;
   if (var) {
      bool vs_in = bld->shader->info.stage == MESA_SHADER_VERTEX &&
         var->data.mode == nir_var_shader_in;
      bool gs_in = bld->shader->info.stage == MESA_SHADER_GEOMETRY &&
         var->data.mode == nir_var_shader_in;
      bool tcs_in = bld->shader->info.stage == MESA_SHADER_TESS_CTRL &&
         var->data.mode == nir_var_shader_in;
      bool tcs_out = bld->shader->info.stage == MESA_SHADER_TESS_CTRL &&
         var->data.mode == nir_var_shader_out && !var->data.patch;
      bool tes_in = bld->shader->info.stage == MESA_SHADER_TESS_EVAL &&
         var->data.mode == nir_var_shader_in && !var->data.patch;

      mode = var->data.mode;

      get_deref_offset(bld, deref, vs_in,
                   gs_in ? &vertex_index : NULL,
                   (tcs_in || tcs_out || tes_in) ? &indir_vertex_index : NULL,
                   &const_index, &indir_index);

      /* Return undef for loads definitely outside of the array bounds
       * (tcs-tes-levels-out-of-bounds-read.shader_test).
       */
      if (var->data.compact && compact_array_index_oob(bld, var, const_index)) {
         struct lp_build_context *undef_bld = get_int_bld(bld, true,
                                                          instr->def.bit_size, true);
         for (int i = 0; i < instr->def.num_components; i++)
            result[i] = LLVMGetUndef(undef_bld->vec_type);
         return;
      }
   }
   emit_load_var(bld, mode, nc, bit_size, var, vertex_index,
                 indir_vertex_index, const_index, indir_index, result);
}

static void
visit_store_var(struct lp_build_nir_soa_context *bld,
                nir_intrinsic_instr *instr)
{
   nir_deref_instr *deref = nir_def_as_deref(instr->src[0].ssa);
   nir_variable *var = nir_deref_instr_get_variable(deref);
   assert(util_bitcount(deref->modes) == 1);
   nir_variable_mode mode = deref->modes;
   int writemask = instr->const_index[0];
   unsigned bit_size = nir_src_bit_size(instr->src[1]);
   LLVMValueRef src[NIR_MAX_VEC_COMPONENTS] = { NULL };
   get_src_vec(bld, 1, src);
   unsigned const_index = 0;
   LLVMValueRef indir_index = NULL, indir_vertex_index = NULL;
   if (var) {
      bool tcs_out = bld->shader->info.stage == MESA_SHADER_TESS_CTRL &&
         var->data.mode == nir_var_shader_out && !var->data.patch;
      bool mesh_out = bld->shader->info.stage == MESA_SHADER_MESH &&
         var->data.mode == nir_var_shader_out;
      get_deref_offset(bld, deref, false, NULL,
                       (tcs_out || mesh_out) ? &indir_vertex_index : NULL,
                       &const_index, &indir_index);

      /* Skip stores definitely outside of the array bounds
       * (tcs-tes-levels-out-of-bounds-write.shader_test).
       */
      if (var->data.compact && compact_array_index_oob(bld, var, const_index))
         return;
   }
   emit_store_var(bld, mode, instr->num_components, bit_size,
                  var, writemask, indir_vertex_index, const_index,
                  indir_index, src);
}

static void
visit_load_ubo(struct lp_build_nir_soa_context *bld,
               nir_intrinsic_instr *instr,
               LLVMValueRef result[NIR_MAX_VEC_COMPONENTS])
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;

   LLVMValueRef index = get_src(bld, &instr->src[0], 0);
   LLVMValueRef offset = get_src(bld, &instr->src[1], 0);

   bool in_bounds = nir_intrinsic_access(instr) & ACCESS_IN_BOUNDS;
   if (!lp_exec_mask_is_nz(bld))
      in_bounds = false;

   struct lp_build_context *uint_bld = get_int_bld(bld, true, 32, lp_value_is_divergent(offset));
   struct lp_build_context *bld_broad = get_int_bld(bld, true, instr->def.bit_size, lp_value_is_divergent(offset));

   LLVMValueRef consts_ptr = lp_llvm_buffer_base(gallivm, bld->consts_ptr, index, LP_MAX_TGSI_CONST_BUFFERS);
   LLVMValueRef num_consts = in_bounds ? NULL : lp_llvm_buffer_num_elements(gallivm, bld->consts_ptr, index, LP_MAX_TGSI_CONST_BUFFERS);

   unsigned size_shift = bit_size_to_shift_size(instr->def.bit_size);
   if (size_shift)
      offset = lp_build_shr(uint_bld, offset, lp_build_const_int_vec(gallivm, uint_bld->type, size_shift));

   LLVMTypeRef ptr_type = LLVMPointerType(bld_broad->elem_type, 0);
   consts_ptr = LLVMBuildBitCast(builder, consts_ptr, ptr_type, "");

   if (!lp_value_is_divergent(offset)) {
      struct lp_build_context *load_bld = get_int_bld(bld, true, instr->def.bit_size, false);

      if (num_consts) {
         switch (instr->def.bit_size) {
         case 8:
            num_consts = LLVMBuildShl(gallivm->builder, num_consts, lp_build_const_int32(gallivm, 2), "");
            break;
         case 16:
            num_consts = LLVMBuildShl(gallivm->builder, num_consts, lp_build_const_int32(gallivm, 1), "");
            break;
         case 64:
            num_consts = LLVMBuildLShr(gallivm->builder, num_consts, lp_build_const_int32(gallivm, 1), "");
            break;
         default: break;
         }
      }

      for (unsigned c = 0; c < instr->def.num_components; c++) {
         LLVMValueRef chan_offset = LLVMBuildAdd(builder, offset, lp_build_const_int32(gallivm, c), "");
         LLVMValueRef ptr = LLVMBuildGEP2(builder, bld_broad->elem_type, consts_ptr, &chan_offset, 1, "");

         if (num_consts) {
            LLVMValueRef in_range = lp_offset_in_range(bld, chan_offset, num_consts);
            LLVMValueRef null_ptr = LLVMBuildBitCast(builder, bld->null_qword_ptr, LLVMTypeOf(ptr), "");
            ptr = LLVMBuildSelect(builder, in_range, ptr, null_ptr, "");
         }

         result[c] = LLVMBuildLoad2(builder, load_bld->elem_type, ptr, "");
      }
   } else {
      if (num_consts) {
         num_consts = lp_build_broadcast_scalar(uint_bld, num_consts);
         if (instr->def.bit_size == 64)
            num_consts = lp_build_shr_imm(uint_bld, num_consts, 1);
         else if (instr->def.bit_size == 16)
            num_consts = lp_build_shl_imm(uint_bld, num_consts, 1);
         else if (instr->def.bit_size == 8)
            num_consts = lp_build_shl_imm(uint_bld, num_consts, 2);
      }

      for (unsigned c = 0; c < instr->def.num_components; c++) {
         LLVMValueRef this_offset = lp_build_add(uint_bld, offset, lp_build_const_int_vec(gallivm, uint_bld->type, c));

         LLVMValueRef overflow_mask = NULL;
         if (num_consts) {
            overflow_mask = lp_build_compare(gallivm, uint_bld->type, PIPE_FUNC_GEQUAL,
                                             this_offset, num_consts);
         }

         result[c] = build_gather(bld, bld_broad, bld_broad->elem_type, consts_ptr, this_offset, overflow_mask, NULL);
      }
   }
}

static void
visit_load_ssbo(struct lp_build_nir_soa_context *bld,
                nir_intrinsic_instr *instr,
                LLVMValueRef result[NIR_MAX_VEC_COMPONENTS])
{
   LLVMValueRef idx = get_src(bld, &instr->src[0], 0);
   idx = cast_type(bld, idx, nir_type_uint, nir_src_bit_size(instr->src[0]));

   LLVMValueRef offset = get_src(bld, &instr->src[1], 0);

   bool in_bounds = nir_intrinsic_access(instr) & ACCESS_IN_BOUNDS;
   if (!lp_exec_mask_is_nz(bld))
      in_bounds = false;

   emit_load_mem(bld, instr->def.num_components,
                 instr->def.bit_size,
                 !lp_nir_instr_src_divergent(&instr->instr, 0),
                 !lp_nir_instr_src_divergent(&instr->instr, 1),
                 false, in_bounds, idx, offset, result);
}

static void
visit_store_ssbo(struct lp_build_nir_soa_context *bld,
                 nir_intrinsic_instr *instr)
{
   LLVMValueRef val[NIR_MAX_VEC_COMPONENTS] = { NULL };
   get_src_vec(bld, 0, val);

   LLVMValueRef idx = get_src(bld, &instr->src[1], 0);
   idx = cast_type(bld, idx, nir_type_uint, nir_src_bit_size(instr->src[1]));

   LLVMValueRef offset = get_src(bld, &instr->src[2], 0);
   int writemask = instr->const_index[0];
   int nc = nir_src_num_components(instr->src[0]);
   int bitsize = nir_src_bit_size(instr->src[0]);

   bool in_bounds = nir_intrinsic_access(instr) & ACCESS_IN_BOUNDS;
   if (!lp_exec_mask_is_nz(bld))
      in_bounds = false;

   emit_store_mem(bld, writemask, nc, bitsize,
                  false, in_bounds, idx, offset, val);
}

static void
visit_get_ssbo_size(struct lp_build_nir_soa_context *bld,
                    nir_intrinsic_instr *instr,
                    LLVMValueRef result[NIR_MAX_VEC_COMPONENTS])
{
   LLVMValueRef idx = get_src(bld, &instr->src[0], 0);
   idx = cast_type(bld, idx, nir_type_uint, nir_src_bit_size(instr->src[0]));

   LLVMValueRef size;
   ssbo_base_pointer(bld, 8, idx, lp_value_is_divergent(idx) ? first_active_invocation(bld, true) : NULL, &size);

   result[0] = size;
}

static void
visit_ssbo_atomic(struct lp_build_nir_soa_context *bld,
                  nir_intrinsic_instr *instr,
                  LLVMValueRef result[NIR_MAX_VEC_COMPONENTS])
{
   LLVMValueRef idx = get_src(bld, &instr->src[0], 0);
   idx = cast_type(bld, idx, nir_type_uint, nir_src_bit_size(instr->src[0]));

   LLVMValueRef offset = get_src(bld, &instr->src[1], 0);
   LLVMValueRef val = get_src(bld, &instr->src[2], 0);
   LLVMValueRef val2 = NULL;
   int bitsize = nir_src_bit_size(instr->src[2]);
   if (instr->intrinsic == nir_intrinsic_ssbo_atomic_swap)
      val2 = get_src(bld, &instr->src[3], 0);

   bool in_bounds = nir_intrinsic_access(instr) & ACCESS_IN_BOUNDS;
   if (bld->exec_mask.has_mask && !nir_src_is_const(instr->src[1]))
      in_bounds = false;

   emit_atomic_mem(bld, nir_intrinsic_atomic_op(instr), bitsize, false,
                   in_bounds, idx, offset, val, val2, &result[0]);
}

static void
img_params_init_resource(struct lp_build_nir_soa_context *bld, struct lp_img_params *params, nir_src *src)
{
   if (nir_src_bit_size(*src) < 64) {
      if (nir_src_is_const(*src))
         params->image_index = nir_src_as_int(*src);
      else
         params->image_index_offset = get_src(bld, src, 0);

      return;
   }

   params->resource = get_src(bld, src, 0);
}

static void
sampler_size_params_init_resource(struct lp_build_nir_soa_context *bld, struct lp_sampler_size_query_params *params, nir_src *src)
{
   if (nir_src_bit_size(*src) < 64) {
      if (nir_src_is_const(*src))
         params->texture_unit = nir_src_as_int(*src);
      else
         params->texture_unit_offset = get_src(bld, src, 0);

      return;
   }

   params->resource = get_src(bld, src, 0);
}

static void
visit_load_image(struct lp_build_nir_soa_context *bld,
                 nir_intrinsic_instr *instr,
                 LLVMValueRef result[NIR_MAX_VEC_COMPONENTS])
{
   LLVMValueRef coords[NIR_MAX_VEC_COMPONENTS] = { NULL };
   get_src_vec(bld, 1, coords);
   for (uint32_t i = nir_image_intrinsic_coord_components(instr); i < 4; i++)
      coords[i] = bld->uint_bld.zero;

   struct lp_img_params params = { 0 };

   /*
    * XXX the target here may not match what is used to compile the image
    * function (which is based on the view) due to array mismatch.
    */
   params.target = glsl_sampler_to_pipe(nir_intrinsic_image_dim(instr),
                                        nir_intrinsic_image_array(instr));
   if (params.target == PIPE_TEXTURE_1D_ARRAY)
      coords[2] = coords[1];

   params.coords = coords;
   params.outdata = result;
   lp_img_op_from_intrinsic(&params, instr);
   params.packed_op = lp_packed_img_op_from_intrinsic(instr);
   if (nir_intrinsic_image_dim(instr) == GLSL_SAMPLER_DIM_MS ||
       nir_intrinsic_image_dim(instr) == GLSL_SAMPLER_DIM_SUBPASS_MS)
      params.ms_index = cast_type(bld, get_src(bld, &instr->src[2], 0),
                                  nir_type_uint, 32);

   img_params_init_resource(bld, &params, &instr->src[0]);

   params.format = nir_intrinsic_format(instr);
   if (instr->def.bit_size == 64)
      params.format = PIPE_FORMAT_R64G64B64A64_UINT;

   emit_image_op(bld, &params);
}


static void
visit_store_image(struct lp_build_nir_soa_context *bld,
                  nir_intrinsic_instr *instr)
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;

   LLVMValueRef coords[NIR_MAX_VEC_COMPONENTS] = { NULL };
   get_src_vec(bld, 1, coords);
   for (uint32_t i = nir_image_intrinsic_coord_components(instr); i < 4; i++)
      coords[i] = bld->uint_bld.zero;

   LLVMValueRef in_val[NIR_MAX_VEC_COMPONENTS] = { NULL };
   get_src_vec(bld, 3, in_val);
   struct lp_img_params params = { 0 };

   params.target = glsl_sampler_to_pipe(nir_intrinsic_image_dim(instr), nir_intrinsic_image_array(instr));
   if (params.target == PIPE_TEXTURE_1D_ARRAY)
      coords[2] = coords[1];
   params.coords = coords;

   params.format = nir_intrinsic_format(instr);
   if (nir_src_bit_size(instr->src[3]) == 64)
      params.format = PIPE_FORMAT_R64G64B64A64_UINT;

   const struct util_format_description *desc = util_format_description(params.format);
   bool integer = desc->channel[util_format_get_first_non_void_channel(params.format)].pure_integer;

   for (unsigned i = 0; i < 4; i++) {
      params.indata[i] = in_val[i];

      if (nir_src_bit_size(instr->src[3]) == 64) {
         assert(integer);
         params.indata[i] = LLVMBuildBitCast(builder, params.indata[i], bld->int64_bld.vec_type, "");
      } else {
         if (integer)
            params.indata[i] = LLVMBuildBitCast(builder, params.indata[i], bld->int_bld.vec_type, "");
         else
            params.indata[i] = LLVMBuildBitCast(builder, params.indata[i], bld->base.vec_type, "");
      }
   }
   if (nir_intrinsic_image_dim(instr) == GLSL_SAMPLER_DIM_MS)
      params.ms_index = get_src(bld, &instr->src[2], 0);
   params.img_op = LP_IMG_STORE;
   params.packed_op = lp_packed_img_op_from_intrinsic(instr);

   img_params_init_resource(bld, &params, &instr->src[0]);

   emit_image_op(bld, &params);
}

LLVMAtomicRMWBinOp
lp_translate_atomic_op(nir_atomic_op op)
{
   switch (op) {
   case nir_atomic_op_iadd: return LLVMAtomicRMWBinOpAdd;
   case nir_atomic_op_xchg: return LLVMAtomicRMWBinOpXchg;
   case nir_atomic_op_iand: return LLVMAtomicRMWBinOpAnd;
   case nir_atomic_op_ior:  return LLVMAtomicRMWBinOpOr;
   case nir_atomic_op_ixor: return LLVMAtomicRMWBinOpXor;
   case nir_atomic_op_umin: return LLVMAtomicRMWBinOpUMin;
   case nir_atomic_op_umax: return LLVMAtomicRMWBinOpUMax;
   case nir_atomic_op_imin: return LLVMAtomicRMWBinOpMin;
   case nir_atomic_op_imax: return LLVMAtomicRMWBinOpMax;
   case nir_atomic_op_fadd: return LLVMAtomicRMWBinOpFAdd;
#if LLVM_VERSION_MAJOR >= 15
   case nir_atomic_op_fmin: return LLVMAtomicRMWBinOpFMin;
   case nir_atomic_op_fmax: return LLVMAtomicRMWBinOpFMax;
#endif
   default:          UNREACHABLE("Unexpected atomic");
   }
}

void
lp_img_op_from_intrinsic(struct lp_img_params *params, nir_intrinsic_instr *instr)
{
   if (instr->intrinsic == nir_intrinsic_image_load ||
       instr->intrinsic == nir_intrinsic_bindless_image_load) {
      params->img_op = LP_IMG_LOAD;
      return;
   }

   if (instr->intrinsic == nir_intrinsic_bindless_image_sparse_load) {
      params->img_op = LP_IMG_LOAD_SPARSE;
      return;
   }

   if (instr->intrinsic == nir_intrinsic_image_store ||
       instr->intrinsic == nir_intrinsic_bindless_image_store) {
      params->img_op = LP_IMG_STORE;
      return;
   }

   if (instr->intrinsic == nir_intrinsic_image_atomic_swap ||
       instr->intrinsic == nir_intrinsic_bindless_image_atomic_swap) {
      params->img_op = LP_IMG_ATOMIC_CAS;
      return;
   }

   if (instr->intrinsic == nir_intrinsic_image_atomic ||
       instr->intrinsic == nir_intrinsic_bindless_image_atomic) {
      params->img_op = LP_IMG_ATOMIC;
      params->op = lp_translate_atomic_op(nir_intrinsic_atomic_op(instr));
   } else {
      params->img_op = -1;
   }
}

uint32_t
lp_packed_img_op_from_intrinsic(nir_intrinsic_instr *instr)
{
   struct lp_img_params params;
   lp_img_op_from_intrinsic(&params, instr);

   if (params.img_op == -1)
      return -1;

   uint32_t op = params.img_op;
   if (op == LP_IMG_ATOMIC_CAS)
      op--;
   else if (op == LP_IMG_ATOMIC)
      op = params.op + (LP_IMG_OP_COUNT - 1);

   uint32_t flags = 0;
   if (nir_intrinsic_image_dim(instr) == GLSL_SAMPLER_DIM_MS ||
       nir_intrinsic_image_dim(instr) == GLSL_SAMPLER_DIM_SUBPASS_MS)
      flags |= LP_IMAGE_OP_MS;

   if (params.img_op == LP_IMG_LOAD || params.img_op == LP_IMG_LOAD_SPARSE) {
      if (instr->def.bit_size == 64)
         flags |= LP_IMAGE_OP_64;
   } else {
      if (nir_src_bit_size(instr->src[3]) == 64)
         flags |= LP_IMAGE_OP_64;
   }

   if (nir_intrinsic_image_array(instr))
      flags |= LP_IMAGE_OP_HAS_LAYER;

   return op + flags * LP_IMAGE_OP_COUNT;
}

static void
visit_atomic_image(struct lp_build_nir_soa_context *bld,
                   nir_intrinsic_instr *instr,
                   LLVMValueRef result[NIR_MAX_VEC_COMPONENTS])
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;
   struct lp_img_params params = { 0 };

   LLVMValueRef coords[NIR_MAX_VEC_COMPONENTS] = { NULL };
   get_src_vec(bld, 1, coords);
   for (uint32_t i = nir_image_intrinsic_coord_components(instr); i < 4; i++)
      coords[i] = bld->uint_bld.zero;

   LLVMValueRef in_val = get_src(bld, &instr->src[3], 0);

   params.target = glsl_sampler_to_pipe(nir_intrinsic_image_dim(instr),
                                        nir_intrinsic_image_array(instr));
   if (params.target == PIPE_TEXTURE_1D_ARRAY) {
      coords[2] = coords[1];
   }

   params.coords = coords;

   params.format = nir_intrinsic_format(instr);
   if (nir_src_bit_size(instr->src[3]) == 64)
      params.format = PIPE_FORMAT_R64G64B64A64_UINT;

   const struct util_format_description *desc = util_format_description(params.format);
   bool integer = desc->channel[util_format_get_first_non_void_channel(params.format)].pure_integer;

   if (nir_intrinsic_image_dim(instr) == GLSL_SAMPLER_DIM_MS)
      params.ms_index = get_src(bld, &instr->src[2], 0);

   LLVMTypeRef indata_type = NULL;
   if (nir_src_bit_size(instr->src[3]) == 64) {
      assert(integer);
      indata_type = bld->int64_bld.vec_type;
   } else {
      if (integer)
         indata_type = bld->int_bld.vec_type;
      else
         indata_type = bld->base.vec_type;
   }

   if (instr->intrinsic == nir_intrinsic_image_atomic_swap ||
       instr->intrinsic == nir_intrinsic_bindless_image_atomic_swap) {
      LLVMValueRef cas_val = get_src(bld, &instr->src[4], 0);
      params.indata[0] = in_val;
      params.indata2[0] = cas_val;
      params.indata2[0] = LLVMBuildBitCast(builder, params.indata2[0], indata_type, "");
   } else {
      params.indata[0] = in_val;
   }

   params.indata[0] = LLVMBuildBitCast(builder, params.indata[0], indata_type, "");

   params.outdata = result;

   lp_img_op_from_intrinsic(&params, instr);
   params.packed_op = lp_packed_img_op_from_intrinsic(instr);

   img_params_init_resource(bld, &params, &instr->src[0]);

   emit_image_op(bld, &params);
}

static void
visit_image_size(struct lp_build_nir_soa_context *bld,
                 nir_intrinsic_instr *instr,
                 LLVMValueRef result[NIR_MAX_VEC_COMPONENTS])
{
   struct lp_sampler_size_query_params params = { 0 };

   sampler_size_params_init_resource(bld, &params, &instr->src[0]);

   params.target = glsl_sampler_to_pipe(nir_intrinsic_image_dim(instr),
                                        nir_intrinsic_image_array(instr));
   params.sizes_out = result;
   params.ms = nir_intrinsic_image_dim(instr) == GLSL_SAMPLER_DIM_MS ||
      nir_intrinsic_image_dim(instr) == GLSL_SAMPLER_DIM_SUBPASS_MS;
   params.format = nir_intrinsic_format(instr);

   emit_image_size(bld, &params);
}

static void
visit_image_samples(struct lp_build_nir_soa_context *bld,
                    nir_intrinsic_instr *instr,
                    LLVMValueRef result[NIR_MAX_VEC_COMPONENTS])
{
   struct lp_sampler_size_query_params params = { 0 };

   sampler_size_params_init_resource(bld, &params, &instr->src[0]);

   params.target = glsl_sampler_to_pipe(nir_intrinsic_image_dim(instr),
                                        nir_intrinsic_image_array(instr));
   params.sizes_out = result;
   params.ms = nir_intrinsic_image_dim(instr) == GLSL_SAMPLER_DIM_MS ||
      nir_intrinsic_image_dim(instr) == GLSL_SAMPLER_DIM_SUBPASS_MS;
   params.samples_only = true;

   params.format = nir_intrinsic_format(instr);

   emit_image_size(bld, &params);
}

static void
visit_shared_load(struct lp_build_nir_soa_context *bld,
                  nir_intrinsic_instr *instr,
                  LLVMValueRef result[NIR_MAX_VEC_COMPONENTS])
{
   LLVMValueRef offset = get_src(bld, &instr->src[0], 0);
   bool offset_is_uniform = !lp_nir_instr_src_divergent(&instr->instr, 0);
   emit_load_mem(bld, instr->def.num_components,
                 instr->def.bit_size, true,
                 offset_is_uniform, false, false, NULL, offset, result);
}

static void
visit_shared_store(struct lp_build_nir_soa_context *bld,
                   nir_intrinsic_instr *instr)
{
   LLVMValueRef val[NIR_MAX_VEC_COMPONENTS] = { NULL };
   get_src_vec(bld, 0, val);
   LLVMValueRef offset = get_src(bld, &instr->src[1], 0);
   int writemask = nir_intrinsic_write_mask(instr);
   int nc = nir_src_num_components(instr->src[0]);
   int bitsize = nir_src_bit_size(instr->src[0]);
   emit_store_mem(bld, writemask, nc, bitsize, false, false, NULL, offset, val);
}

static void
visit_shared_atomic(struct lp_build_nir_soa_context *bld,
                    nir_intrinsic_instr *instr,
                    LLVMValueRef result[NIR_MAX_VEC_COMPONENTS])
{
   LLVMValueRef offset = get_src(bld, &instr->src[0], 0);
   LLVMValueRef val = get_src(bld, &instr->src[1], 0);
   LLVMValueRef val2 = NULL;
   int bitsize = nir_src_bit_size(instr->src[1]);
   if (instr->intrinsic == nir_intrinsic_shared_atomic_swap)
      val2 = get_src(bld, &instr->src[2], 0);

   emit_atomic_mem(bld, nir_intrinsic_atomic_op(instr), bitsize, false, false,
                   NULL, offset, val, val2, &result[0]);
}

static void
visit_barrier(struct lp_build_nir_soa_context *bld,
              nir_intrinsic_instr *instr)
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;
   mesa_scope exec_scope = nir_intrinsic_execution_scope(instr);
   unsigned nir_semantics = nir_intrinsic_memory_semantics(instr);

   if (nir_semantics) {
      LLVMAtomicOrdering ordering = LLVMAtomicOrderingSequentiallyConsistent;
      LLVMBuildFence(builder, ordering, false, "");
   }
   if (exec_scope != SCOPE_NONE) {
      LLVMBasicBlockRef resume = lp_build_insert_new_block(gallivm, "resume");

      lp_build_coro_suspend_switch(gallivm, bld->coro, resume, false);
      LLVMPositionBuilderAtEnd(gallivm->builder, resume);
   }
}

static void
visit_discard(struct lp_build_nir_soa_context *bld,
              nir_intrinsic_instr *instr)
{
   LLVMBuilderRef builder = bld->base.gallivm->builder;

   LLVMValueRef cond = NULL;
   if (instr->intrinsic == nir_intrinsic_demote_if) {
      cond = get_src(bld, &instr->src[0], 0);
      cond = LLVMBuildSExt(builder, cond, bld->uint_bld.vec_type, "");
   }

   LLVMValueRef mask;

   if (!cond) {
      if (bld->exec_mask.has_mask) {
         mask = LLVMBuildNot(builder, bld->exec_mask.exec_mask, "kilp");
      } else {
         mask = LLVMConstNull(bld->base.int_vec_type);
      }
   } else {
      mask = LLVMBuildNot(builder, cond, "");
      if (bld->exec_mask.has_mask) {
         LLVMValueRef invmask;
         invmask = LLVMBuildNot(builder, bld->exec_mask.exec_mask, "kilp");
         mask = LLVMBuildOr(builder, mask, invmask, "");
      }
   }
   lp_build_mask_update(bld->mask, mask);
}

static void
visit_load_global(struct lp_build_nir_soa_context *bld,
                  nir_intrinsic_instr *instr,
                  LLVMValueRef result[NIR_MAX_VEC_COMPONENTS])
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;

   LLVMValueRef addr = get_src(bld, &instr->src[0], 0);

   struct lp_build_context *uint_bld = &bld->uint_bld;
   struct lp_build_context *res_bld;

   res_bld = get_int_bld(bld, true, instr->def.bit_size, lp_value_is_divergent(addr));

   if (!lp_value_is_divergent(addr)) {
      addr = global_addr_to_ptr(gallivm, addr, instr->def.bit_size);

      for (unsigned c = 0; c < instr->def.num_components; c++) {
         result[c] = lp_build_pointer_get2(builder, res_bld->elem_type,
                                           addr, lp_build_const_int32(gallivm, c));
      }
      return;
   }

   LLVMValueRef mask = mask_vec_with_helpers(bld);
   for (unsigned c = 0; c < instr->def.num_components; c++) {
      LLVMValueRef chan_offset = lp_build_const_int_vec(gallivm, uint_bld->type, c * (instr->def.bit_size / 8));

      result[c] = lp_build_masked_gather(gallivm, res_bld->type.length, instr->def.bit_size, res_bld->vec_type,
                                         lp_vec_add_offset_ptr(bld, instr->def.bit_size, addr, chan_offset),
                                         mask);
      result[c] = LLVMBuildBitCast(builder, result[c], res_bld->vec_type, "");
   }
}

static void
visit_store_global(struct lp_build_nir_soa_context *bld,
                   nir_intrinsic_instr *instr)
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;

   LLVMValueRef dst[NIR_MAX_VEC_COMPONENTS] = { NULL };
   get_src_vec(bld, 0, dst);
   int nc = nir_src_num_components(instr->src[0]);
   int bit_size = nir_src_bit_size(instr->src[0]);
   LLVMValueRef addr = get_src(bld, &instr->src[1], 0);
   int writemask = instr->const_index[0];

   struct lp_build_context *uint_bld = &bld->uint_bld;
   LLVMValueRef exec_mask = mask_vec(bld);

   for (unsigned c = 0; c < nc; c++) {
      if (!(writemask & (1u << c)))
         continue;
      LLVMValueRef val = dst[c];
      LLVMValueRef chan_offset = lp_build_const_int_vec(gallivm, uint_bld->type, c * (bit_size / 8));

      struct lp_build_context *out_bld = get_int_bld(bld, false, bit_size, lp_value_is_divergent(val));
      val = LLVMBuildBitCast(builder, val, out_bld->vec_type, "");
      lp_build_masked_scatter(gallivm, out_bld->type.length, bit_size,
                              lp_vec_add_offset_ptr(bld, bit_size,
                                                    addr, chan_offset),
                              val, exec_mask);
   }
}

static void
visit_global_atomic(struct lp_build_nir_soa_context *bld,
                    nir_intrinsic_instr *instr,
                    LLVMValueRef result[NIR_MAX_VEC_COMPONENTS])
{

   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;

   LLVMValueRef addr = get_src(bld, &instr->src[0], 0);
   LLVMValueRef val = get_src(bld, &instr->src[1], 0);
   LLVMValueRef val2 = NULL;
   int val_bit_size = nir_src_bit_size(instr->src[1]);
   if (instr->intrinsic == nir_intrinsic_global_atomic_swap)
      val2 = get_src(bld, &instr->src[2], 0);

   struct lp_build_context *uint_bld = &bld->uint_bld;
   bool is_flt = nir_atomic_op_type(nir_intrinsic_atomic_op(instr)) == nir_type_float;
   struct lp_build_context *atom_bld = is_flt ? get_flt_bld(bld, val_bit_size, true) : get_int_bld(bld, true, val_bit_size, true);
   if (is_flt)
      val = LLVMBuildBitCast(builder, val, atom_bld->vec_type, "");

   LLVMValueRef atom_res = lp_build_alloca(gallivm,
                                           atom_bld->vec_type, "");
   LLVMValueRef exec_mask = mask_vec(bld);
   struct lp_build_loop_state loop_state;
   lp_build_loop_begin(&loop_state, gallivm, lp_build_const_int32(gallivm, 0));

   LLVMValueRef value_ptr = LLVMBuildExtractElement(gallivm->builder, val,
                                                    loop_state.counter, "");
   value_ptr = LLVMBuildBitCast(gallivm->builder, value_ptr, atom_bld->elem_type, "");

   LLVMValueRef addr_ptr = LLVMBuildExtractElement(gallivm->builder, addr,
                                                   loop_state.counter, "");
   addr_ptr = global_addr_to_ptr(gallivm, addr_ptr, 32);
   struct lp_build_if_state ifthen;
   LLVMValueRef cond, temp_res;
   LLVMValueRef scalar;
   cond = LLVMBuildICmp(gallivm->builder, LLVMIntNE, exec_mask, uint_bld->zero, "");
   cond = LLVMBuildExtractElement(gallivm->builder, cond, loop_state.counter, "");
   lp_build_if(&ifthen, gallivm, cond);

   addr_ptr = LLVMBuildBitCast(gallivm->builder, addr_ptr, LLVMPointerType(LLVMTypeOf(value_ptr), 0), "");
   if (val2 != NULL /* compare-and-swap */) {
      LLVMValueRef cas_src_ptr = LLVMBuildExtractElement(gallivm->builder, val2,
                                                         loop_state.counter, "");
      cas_src_ptr = LLVMBuildBitCast(gallivm->builder, cas_src_ptr, atom_bld->elem_type, "");
      scalar = LLVMBuildAtomicCmpXchg(builder, addr_ptr, value_ptr,
                                      cas_src_ptr,
                                      LLVMAtomicOrderingSequentiallyConsistent,
                                      LLVMAtomicOrderingSequentiallyConsistent,
                                      false);
      scalar = LLVMBuildExtractValue(gallivm->builder, scalar, 0, "");
   } else {
      scalar = LLVMBuildAtomicRMW(builder, lp_translate_atomic_op(nir_intrinsic_atomic_op(instr)),
                                  addr_ptr, value_ptr,
                                  LLVMAtomicOrderingSequentiallyConsistent,
                                  false);
   }
   temp_res = LLVMBuildLoad2(builder, atom_bld->vec_type, atom_res, "");
   temp_res = LLVMBuildInsertElement(builder, temp_res, scalar, loop_state.counter, "");
   LLVMBuildStore(builder, temp_res, atom_res);
   lp_build_else(&ifthen);
   temp_res = LLVMBuildLoad2(builder, atom_bld->vec_type, atom_res, "");
   LLVMValueRef zero_val = lp_build_zero_bits(gallivm, val_bit_size, is_flt);
   temp_res = LLVMBuildInsertElement(builder, temp_res, zero_val, loop_state.counter, "");
   LLVMBuildStore(builder, temp_res, atom_res);
   lp_build_endif(&ifthen);
   lp_build_loop_end_cond(&loop_state, lp_build_const_int32(gallivm, uint_bld->type.length),
                          NULL, LLVMIntUGE);
   *result = LLVMBuildLoad2(builder, LLVMTypeOf(val), atom_res, "");
}

#if LLVM_VERSION_MAJOR >= 10
static void visit_shuffle(struct lp_build_nir_soa_context *bld,
                          nir_intrinsic_instr *instr,
                          LLVMValueRef result[NIR_MAX_VEC_COMPONENTS])
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;

   LLVMValueRef src = get_src(bld, &instr->src[0], 0);
   src = cast_type(bld, src, nir_type_int,
                   nir_src_bit_size(instr->src[0]));
   LLVMValueRef index = get_src(bld, &instr->src[1], 0);
   index = cast_type(bld, index, nir_type_uint,
                     nir_src_bit_size(instr->src[1]));

   uint32_t bit_size = nir_src_bit_size(instr->src[0]);
   uint32_t index_bit_size = nir_src_bit_size(instr->src[1]);
   struct lp_build_context *int_bld = get_int_bld(bld, true, bit_size, true);

   if (util_get_cpu_caps()->has_avx2 && bit_size == 32 && index_bit_size == 32 && int_bld->type.length == 8) {
      /* freeze `src` in case inactive invocations contain poison */
      src = LLVMBuildFreeze(builder, src, "");
      result[0] = lp_build_intrinsic_binary(builder, "llvm.x86.avx2.permd", int_bld->vec_type, src, index);
   } else {
      LLVMValueRef res_store = lp_build_alloca(gallivm, int_bld->vec_type, "");
      struct lp_build_loop_state loop_state;
      lp_build_loop_begin(&loop_state, gallivm, lp_build_const_int32(gallivm, 0));

      LLVMValueRef index_value = LLVMBuildExtractElement(builder, index, loop_state.counter, "");

      LLVMValueRef src_value = LLVMBuildExtractElement(builder, src, index_value, "");
      /* freeze `src_value` in case an out-of-bounds index or an index into an
       * inactive invocation results in poison
       */
      src_value = LLVMBuildFreeze(builder, src_value, "");

      LLVMValueRef res = LLVMBuildLoad2(builder, int_bld->vec_type, res_store, "");
      res = LLVMBuildInsertElement(builder, res, src_value, loop_state.counter, "");
      LLVMBuildStore(builder, res, res_store);

      lp_build_loop_end_cond(&loop_state, lp_build_const_int32(gallivm, bld->uint_bld.type.length),
                             NULL, LLVMIntUGE);

      result[0] = LLVMBuildLoad2(builder, int_bld->vec_type, res_store, "");
   }
}
#endif

static void
visit_interp(struct lp_build_nir_soa_context *bld,
             nir_intrinsic_instr *instr,
             LLVMValueRef result[NIR_MAX_VEC_COMPONENTS])
{
   nir_deref_instr *deref = nir_def_as_deref(instr->src[0].ssa);
   unsigned num_components = instr->def.num_components;
   nir_variable *var = nir_deref_instr_get_variable(deref);
   unsigned const_index;
   LLVMValueRef indir_index;
   LLVMValueRef offsets[2] = { NULL, NULL };
   get_deref_offset(bld, deref, false, NULL, NULL,
                    &const_index, &indir_index);
   bool centroid = instr->intrinsic == nir_intrinsic_interp_deref_at_centroid;
   bool sample = false;
   if (instr->intrinsic == nir_intrinsic_interp_deref_at_offset) {
      for (unsigned i = 0; i < 2; i++) {
         offsets[i] = get_src(bld, &instr->src[1], i);
         offsets[i] = cast_type(bld, offsets[i], nir_type_float, 32);
      }
   } else if (instr->intrinsic == nir_intrinsic_interp_deref_at_sample) {
      offsets[0] = get_src(bld, &instr->src[1], 0);
      offsets[0] = cast_type(bld, offsets[0], nir_type_int, 32);
      sample = true;
   }

   for (unsigned i = 0; i < num_components; i++) {
      result[i] = bld->fs_iface->interp_fn(bld->fs_iface, &bld->base,
                                           const_index + var->data.driver_location, i + var->data.location_frac,
                                           centroid, sample, indir_index, offsets);
   }
}

static void
visit_load_scratch(struct lp_build_nir_soa_context *bld,
                   nir_intrinsic_instr *instr,
                   LLVMValueRef result[NIR_MAX_VEC_COMPONENTS])
{
   struct gallivm_state * gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;

   LLVMValueRef offset = get_src(bld, &instr->src[0], 0);

   struct lp_build_context *uint_bld = lp_value_is_divergent(offset) ?
      &bld->uint_bld : &bld->scalar_uint_bld;
   struct lp_build_context *load_bld;
   LLVMValueRef thread_offsets = get_scratch_thread_offsets(gallivm, uint_bld->type, bld->scratch_size);
   LLVMValueRef exec_mask = mask_vec(bld);
   LLVMValueRef scratch_ptr_vec = lp_build_broadcast(gallivm,
                                                     LLVMVectorType(LLVMPointerType(LLVMInt8TypeInContext(gallivm->context), 0), uint_bld->type.length),
                                                     bld->scratch_ptr);
   load_bld = get_int_bld(bld, true, instr->def.bit_size, lp_value_is_divergent(offset));

   offset = lp_build_add(uint_bld, offset, thread_offsets);

   for (unsigned c = 0; c < instr->def.num_components; c++) {
      LLVMValueRef chan_offset = lp_build_add(uint_bld, offset, lp_build_const_int_vec(gallivm, uint_bld->type, c * (instr->def.bit_size / 8)));

      result[c] = lp_build_masked_gather(gallivm, load_bld->type.length, instr->def.bit_size,
                                         load_bld->vec_type,
                                         lp_vec_add_offset_ptr(bld, instr->def.bit_size,
                                                               scratch_ptr_vec,
                                                               chan_offset),
                                         exec_mask);
      result[c] = LLVMBuildBitCast(builder, result[c], load_bld->vec_type, "");
   }
}

static void
visit_store_scratch(struct lp_build_nir_soa_context *bld,
                    nir_intrinsic_instr *instr)
{
   struct gallivm_state * gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;

   LLVMValueRef dst[NIR_MAX_VEC_COMPONENTS] = { NULL };
   get_src_vec(bld, 0, dst);
   LLVMValueRef offset = get_src(bld, &instr->src[1], 0);
   int writemask = instr->const_index[2];
   int nc = nir_src_num_components(instr->src[0]);
   int bit_size = nir_src_bit_size(instr->src[0]);

   struct lp_build_context *uint_bld = &bld->uint_bld;
   struct lp_build_context *store_bld;
   LLVMValueRef thread_offsets = get_scratch_thread_offsets(gallivm, uint_bld->type, bld->scratch_size);
   LLVMValueRef scratch_ptr_vec = lp_build_broadcast(gallivm,
                                                     LLVMVectorType(LLVMPointerType(LLVMInt8TypeInContext(gallivm->context), 0), uint_bld->type.length),
                                                     bld->scratch_ptr);
   store_bld = get_int_bld(bld, true, bit_size, lp_value_is_divergent(offset));

   LLVMValueRef exec_mask = mask_vec(bld);
   offset = lp_build_add(uint_bld, offset, thread_offsets);

   for (unsigned c = 0; c < nc; c++) {
      if (!(writemask & (1u << c)))
         continue;
      LLVMValueRef val = dst[c];

      LLVMValueRef chan_offset = lp_build_add(uint_bld, offset, lp_build_const_int_vec(gallivm, uint_bld->type, c * (bit_size / 8)));

      val = LLVMBuildBitCast(builder, val, store_bld->vec_type, "");

      lp_build_masked_scatter(gallivm, store_bld->type.length, bit_size,
                              lp_vec_add_offset_ptr(bld, bit_size,
                                                    scratch_ptr_vec, chan_offset),
                              val, exec_mask);
   }
}

static void
visit_payload_load(struct lp_build_nir_soa_context *bld,
                  nir_intrinsic_instr *instr,
                  LLVMValueRef result[NIR_MAX_VEC_COMPONENTS])
{
   LLVMValueRef offset = get_src(bld, &instr->src[0], 0);
   bool offset_is_uniform = !lp_nir_instr_src_divergent(&instr->instr, 0);
   emit_load_mem(bld, instr->def.num_components,
                 instr->def.bit_size, true,
                 offset_is_uniform, true, true, NULL, offset, result);
}

static void
visit_payload_store(struct lp_build_nir_soa_context *bld,
                    nir_intrinsic_instr *instr)
{
   LLVMValueRef val[NIR_MAX_VEC_COMPONENTS] = { NULL };
   get_src_vec(bld, 0, val);
   LLVMValueRef offset = get_src(bld, &instr->src[1], 0);
   int writemask = nir_intrinsic_write_mask(instr);
   int nc = nir_src_num_components(instr->src[0]);
   int bitsize = nir_src_bit_size(instr->src[0]);
   emit_store_mem(bld, writemask, nc, bitsize, true, true, NULL, offset, val);
}

static void
visit_payload_atomic(struct lp_build_nir_soa_context *bld,
                     nir_intrinsic_instr *instr,
                     LLVMValueRef result[NIR_MAX_VEC_COMPONENTS])
{
   LLVMValueRef offset = get_src(bld, &instr->src[0], 0);
   LLVMValueRef val = get_src(bld, &instr->src[1], 0);
   LLVMValueRef val2 = NULL;
   int bitsize = nir_src_bit_size(instr->src[1]);
   if (instr->intrinsic == nir_intrinsic_task_payload_atomic_swap)
      val2 = get_src(bld, &instr->src[2], 0);

   emit_atomic_mem(bld, nir_intrinsic_atomic_op(instr), bitsize, true, true,
                   NULL, offset, val, val2, &result[0]);
}

static void visit_load_param(struct lp_build_nir_soa_context *bld,
                             nir_intrinsic_instr *instr,
                             LLVMValueRef result[NIR_MAX_VEC_COMPONENTS])
{
   LLVMValueRef param = LLVMGetParam(bld->func, nir_intrinsic_param_idx(instr) + LP_RESV_FUNC_ARGS);
   struct gallivm_state *gallivm = bld->base.gallivm;
   if (instr->num_components == 1)
      result[0] = param;
   else {
      for (unsigned i = 0; i < instr->num_components; i++)
         result[i] = LLVMBuildExtractValue(gallivm->builder, param, i, "");
   }
}

static void
visit_intrinsic(struct lp_build_nir_soa_context *bld,
                nir_intrinsic_instr *instr)
{
   LLVMValueRef result[NIR_MAX_VEC_COMPONENTS] = {0};
   switch (instr->intrinsic) {
   case nir_intrinsic_decl_reg:
      /* already handled */
      break;
   case nir_intrinsic_load_reg:
   case nir_intrinsic_load_reg_indirect:
      visit_load_reg(bld, instr, result);
      break;
   case nir_intrinsic_store_reg:
   case nir_intrinsic_store_reg_indirect:
      visit_store_reg(bld, instr);
      break;
   case nir_intrinsic_load_input:
   case nir_intrinsic_load_per_primitive_input:
      visit_load_input(bld, instr, result);
      break;
   case nir_intrinsic_store_output:
      visit_store_output(bld, instr);
      break;
   case nir_intrinsic_load_deref:
      visit_load_var(bld, instr, result);
      break;
   case nir_intrinsic_store_deref:
      visit_store_var(bld, instr);
      break;
   case nir_intrinsic_load_ubo:
      visit_load_ubo(bld, instr, result);
      break;
   case nir_intrinsic_load_ssbo:
      visit_load_ssbo(bld, instr, result);
      break;
   case nir_intrinsic_store_ssbo:
      visit_store_ssbo(bld, instr);
      break;
   case nir_intrinsic_get_ssbo_size:
      visit_get_ssbo_size(bld, instr, result);
      break;
   case nir_intrinsic_load_vertex_id:
   case nir_intrinsic_load_primitive_id:
   case nir_intrinsic_load_instance_id:
   case nir_intrinsic_load_base_instance:
   case nir_intrinsic_load_base_vertex:
   case nir_intrinsic_load_first_vertex:
   case nir_intrinsic_load_workgroup_id:
   case nir_intrinsic_load_local_invocation_id:
   case nir_intrinsic_load_local_invocation_index:
   case nir_intrinsic_load_num_workgroups:
   case nir_intrinsic_load_invocation_id:
   case nir_intrinsic_load_front_face:
   case nir_intrinsic_load_draw_id:
   case nir_intrinsic_load_workgroup_size:
   case nir_intrinsic_load_work_dim:
   case nir_intrinsic_load_tess_coord:
   case nir_intrinsic_load_tess_level_outer:
   case nir_intrinsic_load_tess_level_inner:
   case nir_intrinsic_load_patch_vertices_in:
   case nir_intrinsic_load_sample_id:
   case nir_intrinsic_load_sample_pos:
   case nir_intrinsic_load_sample_mask_in:
   case nir_intrinsic_load_view_index:
   case nir_intrinsic_load_subgroup_invocation:
   case nir_intrinsic_load_subgroup_id:
   case nir_intrinsic_load_num_subgroups:
      emit_sysval_intrin(bld, instr, result);
      break;
   case nir_intrinsic_load_helper_invocation:
      emit_helper_invocation(bld, &result[0]);
      break;
   case nir_intrinsic_demote_if:
   case nir_intrinsic_demote:
      visit_discard(bld, instr);
      break;
   case nir_intrinsic_emit_vertex:
      emit_vertex(bld, nir_intrinsic_stream_id(instr));
      break;
   case nir_intrinsic_end_primitive:
      end_primitive(bld, nir_intrinsic_stream_id(instr));
      break;
   case nir_intrinsic_ssbo_atomic:
   case nir_intrinsic_ssbo_atomic_swap:
      visit_ssbo_atomic(bld, instr, result);
      break;
   case nir_intrinsic_image_load:
   case nir_intrinsic_bindless_image_load:
   case nir_intrinsic_bindless_image_sparse_load:
      visit_load_image(bld, instr, result);
      break;
   case nir_intrinsic_image_store:
   case nir_intrinsic_bindless_image_store:
      visit_store_image(bld, instr);
      break;
   case nir_intrinsic_image_atomic:
   case nir_intrinsic_image_atomic_swap:
   case nir_intrinsic_bindless_image_atomic:
   case nir_intrinsic_bindless_image_atomic_swap:
      visit_atomic_image(bld, instr, result);
      break;
   case nir_intrinsic_image_size:
   case nir_intrinsic_bindless_image_size:
      visit_image_size(bld, instr, result);
      break;
   case nir_intrinsic_image_samples:
   case nir_intrinsic_bindless_image_samples:
      visit_image_samples(bld, instr, result);
      break;
   case nir_intrinsic_load_shared:
      visit_shared_load(bld, instr, result);
      break;
   case nir_intrinsic_store_shared:
      visit_shared_store(bld, instr);
      break;
   case nir_intrinsic_shared_atomic:
   case nir_intrinsic_shared_atomic_swap:
      visit_shared_atomic(bld, instr, result);
      break;
   case nir_intrinsic_barrier:
      visit_barrier(bld, instr);
      break;
   case nir_intrinsic_load_global:
   case nir_intrinsic_load_global_constant:
      visit_load_global(bld, instr, result);
      break;
   case nir_intrinsic_store_global:
      visit_store_global(bld, instr);
      break;
   case nir_intrinsic_global_atomic:
   case nir_intrinsic_global_atomic_swap:
      visit_global_atomic(bld, instr, result);
      break;
   case nir_intrinsic_vote_all:
   case nir_intrinsic_vote_any:
   case nir_intrinsic_vote_ieq:
   case nir_intrinsic_vote_feq:
      emit_vote(bld, cast_type(bld, get_src(bld, &instr->src[0], 0), nir_type_int, nir_src_bit_size(instr->src[0])), instr, result);
      break;
   case nir_intrinsic_elect:
      emit_elect(bld, result);
      break;
   case nir_intrinsic_reduce:
   case nir_intrinsic_inclusive_scan:
   case nir_intrinsic_exclusive_scan:
      emit_reduce(bld, cast_type(bld, get_src(bld, &instr->src[0], 0), nir_type_int, nir_src_bit_size(instr->src[0])), instr, result);
      break;
   case nir_intrinsic_ballot:
      emit_ballot(bld, get_src(bld, &instr->src[0], 0), instr, result);
      break;
#if LLVM_VERSION_MAJOR >= 10
   case nir_intrinsic_shuffle:
      visit_shuffle(bld, instr, result);
      break;
#endif
   case nir_intrinsic_read_invocation:
   case nir_intrinsic_read_first_invocation: {
      LLVMValueRef src0 = get_src(bld, &instr->src[0], 0);
      src0 = cast_type(bld, src0, nir_type_int, nir_src_bit_size(instr->src[0]));

      LLVMValueRef src1 = NULL;
      if (instr->intrinsic == nir_intrinsic_read_invocation)
         src1 = cast_type(bld, get_src(bld, &instr->src[1], 0), nir_type_int, nir_src_bit_size(instr->src[1]));

      emit_read_invocation(bld, src0, nir_src_bit_size(instr->src[0]), src1, result);
      break;
   }
   case nir_intrinsic_interp_deref_at_offset:
   case nir_intrinsic_interp_deref_at_centroid:
   case nir_intrinsic_interp_deref_at_sample:
      visit_interp(bld, instr, result);
      break;
   case nir_intrinsic_load_scratch:
      visit_load_scratch(bld, instr, result);
      break;
   case nir_intrinsic_store_scratch:
      visit_store_scratch(bld, instr);
      break;
   case nir_intrinsic_shader_clock:
      emit_clock(bld, result);
      break;
   case nir_intrinsic_launch_mesh_workgroups: {
      LLVMValueRef launch_grid[NIR_MAX_VEC_COMPONENTS] = { NULL };
      get_src_vec(bld, 0, launch_grid);
      emit_launch_mesh_workgroups(bld, launch_grid);
      break;
   }
   case nir_intrinsic_load_task_payload:
      visit_payload_load(bld, instr, result);
      break;
   case nir_intrinsic_store_task_payload:
      visit_payload_store(bld, instr);
      break;
   case nir_intrinsic_task_payload_atomic:
   case nir_intrinsic_task_payload_atomic_swap:
      visit_payload_atomic(bld, instr, result);
      break;
   case nir_intrinsic_set_vertex_and_primitive_count:
      emit_set_vertex_and_primitive_count(bld,
                                          get_src(bld, &instr->src[0], 0),
                                          get_src(bld, &instr->src[1], 0));
      break;
   case nir_intrinsic_load_param:
      visit_load_param(bld, instr, result);
      break;
   case nir_intrinsic_ddx:
   case nir_intrinsic_ddy:
   case nir_intrinsic_ddx_coarse:
   case nir_intrinsic_ddy_coarse:
   case nir_intrinsic_ddx_fine:
   case nir_intrinsic_ddy_fine: {
      LLVMValueRef src = get_src(bld, &instr->src[0], 0);
      src = cast_type(bld, src, nir_type_float, nir_src_bit_size(instr->src[0]));

      struct lp_build_context *float_bld = get_flt_bld(bld, nir_src_bit_size(instr->src[0]), true);

      if (instr->intrinsic == nir_intrinsic_ddx ||
          instr->intrinsic == nir_intrinsic_ddx_coarse ||
          instr->intrinsic == nir_intrinsic_ddx_fine)
         result[0] = lp_build_ddx(float_bld, src);
      else
         result[0] = lp_build_ddy(float_bld, src);

      break;
   }
   case nir_intrinsic_load_const_buf_base_addr_lvp: {
      result[0] = load_ubo_base_addr(bld, get_src(bld, &instr->src[0], 0));
      break;
   }
   case nir_intrinsic_begin_invocation_interlock:
   case nir_intrinsic_end_invocation_interlock:
      /* There is no need to do any synchronization here since llvmpipe rasterizes
       * geometry using tiles and each tile is handled by only one thread at once.
       */
      break;
   default:
      fprintf(stderr, "Unsupported intrinsic: ");
      nir_print_instr(&instr->instr, stderr);
      fprintf(stderr, "\n");
      assert(0);
      break;
   }
   if (result[0]) {
      assign_ssa_dest(bld, &instr->def, result);
   }
}

static void
visit_txs(struct lp_build_nir_soa_context *bld, nir_tex_instr *instr)
{
   struct lp_sampler_size_query_params params = { 0 };
   LLVMValueRef sizes_out[NIR_MAX_VEC_COMPONENTS];
   LLVMValueRef explicit_lod = NULL;
   LLVMValueRef texture_unit_offset = NULL;
   LLVMValueRef resource = NULL;

   for (unsigned i = 0; i < instr->num_srcs; i++) {
      switch (instr->src[i].src_type) {
      case nir_tex_src_lod:
         explicit_lod = cast_type(bld,
                                  get_src(bld, &instr->src[i].src, 0),
                                  nir_type_int, 32);
         break;
      case nir_tex_src_texture_offset:
         texture_unit_offset = get_src(bld, &instr->src[i].src, 0);
         break;
      case nir_tex_src_texture_handle:
         resource = get_src(bld, &instr->src[i].src, 0);
         break;
      default:
         break;
      }
   }

   params.target = glsl_sampler_to_pipe(instr->sampler_dim, instr->is_array);
   params.texture_unit = instr->texture_index;
   params.explicit_lod = explicit_lod;
   params.is_sviewinfo = true;
   params.sizes_out = sizes_out;
   params.samples_only = (instr->op == nir_texop_texture_samples);
   params.texture_unit_offset = texture_unit_offset;
   params.ms = instr->sampler_dim == GLSL_SAMPLER_DIM_MS ||
      instr->sampler_dim == GLSL_SAMPLER_DIM_SUBPASS_MS;

   if (instr->op == nir_texop_query_levels)
      params.explicit_lod = bld->uint_bld.zero;

   params.resource = resource;

   emit_tex_size(bld, &params);
   assign_ssa_dest(bld, &instr->def,
                   &sizes_out[instr->op == nir_texop_query_levels ? 3 : 0]);
}

static enum lp_sampler_lod_property
lp_build_nir_lod_property(mesa_shader_stage stage, nir_src lod_src)
{
   enum lp_sampler_lod_property lod_property;

   if (nir_src_is_always_uniform(lod_src)) {
      lod_property = LP_SAMPLER_LOD_SCALAR;
   } else if (stage == MESA_SHADER_FRAGMENT) {
      if (gallivm_perf & GALLIVM_PERF_NO_QUAD_LOD)
         lod_property = LP_SAMPLER_LOD_PER_ELEMENT;
      else
         lod_property = LP_SAMPLER_LOD_PER_QUAD;
   } else {
      lod_property = LP_SAMPLER_LOD_PER_ELEMENT;
   }
   return lod_property;
}

uint32_t
lp_build_nir_sample_key(mesa_shader_stage stage, nir_tex_instr *instr)
{
   uint32_t sample_key = 0;

   if (instr->op == nir_texop_txf ||
       instr->op == nir_texop_txf_ms) {
      sample_key |= LP_SAMPLER_OP_FETCH << LP_SAMPLER_OP_TYPE_SHIFT;
   } else if (instr->op == nir_texop_tg4) {
      sample_key |= LP_SAMPLER_OP_GATHER << LP_SAMPLER_OP_TYPE_SHIFT;
      sample_key |= (instr->component << LP_SAMPLER_GATHER_COMP_SHIFT);
   } else if (instr->op == nir_texop_lod) {
      sample_key |= LP_SAMPLER_OP_LODQ << LP_SAMPLER_OP_TYPE_SHIFT;
   }

   if (instr->is_array)
      sample_key |= LP_SAMPLER_HAS_LAYER;

   bool explicit_lod = false;
   uint32_t lod_src = 0;

   for (unsigned i = 0; i < instr->num_srcs; i++) {
      switch (instr->src[i].src_type) {
      case nir_tex_src_comparator:
         sample_key |= LP_SAMPLER_SHADOW;
         break;
      case nir_tex_src_bias:
         sample_key |= LP_SAMPLER_LOD_BIAS << LP_SAMPLER_LOD_CONTROL_SHIFT;
         explicit_lod = true;
         lod_src = i;
         break;
      case nir_tex_src_lod:
         sample_key |= LP_SAMPLER_LOD_EXPLICIT << LP_SAMPLER_LOD_CONTROL_SHIFT;
         explicit_lod = true;
         lod_src = i;
         break;
      case nir_tex_src_min_lod:
         sample_key |= LP_SAMPLER_MIN_LOD;
         explicit_lod = true;
         lod_src = i;
         break;
      case nir_tex_src_offset:
         sample_key |= LP_SAMPLER_OFFSETS;
         break;
      case nir_tex_src_ms_index:
         sample_key |= LP_SAMPLER_FETCH_MS;
         break;
      default:
         break;
      }
   }

   enum lp_sampler_lod_property lod_property = LP_SAMPLER_LOD_SCALAR;
   if (explicit_lod)
      lod_property = lp_build_nir_lod_property(stage, instr->src[lod_src].src);

   if (instr->op == nir_texop_txd) {
      sample_key |= LP_SAMPLER_LOD_DERIVATIVES << LP_SAMPLER_LOD_CONTROL_SHIFT;

      if (stage == MESA_SHADER_FRAGMENT) {
         if (gallivm_perf & GALLIVM_PERF_NO_QUAD_LOD)
            lod_property = LP_SAMPLER_LOD_PER_ELEMENT;
         else
            lod_property = LP_SAMPLER_LOD_PER_QUAD;
      } else
         lod_property = LP_SAMPLER_LOD_PER_ELEMENT;
   }

   sample_key |= lod_property << LP_SAMPLER_LOD_PROPERTY_SHIFT;

   if (instr->is_sparse)
      sample_key |= LP_SAMPLER_RESIDENCY;

   return sample_key;
}

static void
visit_tex(struct lp_build_nir_soa_context *bld, nir_tex_instr *instr)
{
   if (instr->op == nir_texop_txs ||
       instr->op == nir_texop_query_levels ||
       instr->op == nir_texop_texture_samples) {
      visit_txs(bld, instr);
      return;
   }

   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;
   LLVMValueRef coords[5];
   LLVMValueRef offsets[3] = { NULL };
   LLVMValueRef explicit_lod = NULL, ms_index = NULL;
   struct lp_sampler_params params = { 0 };
   struct lp_derivatives derivs;
   nir_deref_instr *texture_deref_instr = NULL;
   nir_deref_instr *sampler_deref_instr = NULL;
   LLVMValueRef texture_unit_offset = NULL;
   LLVMValueRef texel[NIR_MAX_VEC_COMPONENTS];
   LLVMValueRef coord_zero = bld->base.zero;
   unsigned coord_vals = instr->coord_components;

   LLVMValueRef texture_resource = NULL;
   LLVMValueRef sampler_resource = NULL;

   for (unsigned i = 0; i < instr->num_srcs; i++) {
      switch (instr->src[i].src_type) {
      case nir_tex_src_coord: {
         get_src_vec(bld, i, coords);
         for (unsigned chan = coord_vals; chan < 5; chan++)
            coords[chan] = coord_zero;
         break;
      }
      case nir_tex_src_texture_deref:
         texture_deref_instr = nir_src_as_deref(instr->src[i].src);
         break;
      case nir_tex_src_sampler_deref:
         sampler_deref_instr = nir_src_as_deref(instr->src[i].src);
         break;
      case nir_tex_src_comparator:
         coords[4] = get_src(bld, &instr->src[i].src, 0);
         coords[4] = cast_type(bld, coords[4], nir_type_float, 32);
         break;
      case nir_tex_src_bias:
         explicit_lod = cast_type(bld, get_src(bld, &instr->src[i].src, 0), nir_type_float, 32);
         break;
      case nir_tex_src_lod:
         if (instr->op == nir_texop_txf)
            explicit_lod = cast_type(bld, get_src(bld, &instr->src[i].src, 0), nir_type_int, 32);
         else
            explicit_lod = cast_type(bld, get_src(bld, &instr->src[i].src, 0), nir_type_float, 32);
         break;
      case nir_tex_src_min_lod:
         params.min_lod = cast_type(bld, get_src(bld, &instr->src[i].src, 0), nir_type_float, 32);
         break;
      case nir_tex_src_ddx: {
         int deriv_cnt = instr->coord_components;
         if (instr->is_array)
            deriv_cnt--;
         LLVMValueRef deriv_vec[NIR_MAX_VEC_COMPONENTS] = { NULL };
         get_src_vec(bld, i, deriv_vec);
         for (unsigned chan = 0; chan < deriv_cnt; ++chan)
            derivs.ddx[chan] = deriv_vec[chan];
         for (unsigned chan = 0; chan < deriv_cnt; ++chan)
            derivs.ddx[chan] = cast_type(bld, derivs.ddx[chan], nir_type_float, 32);
         break;
      }
      case nir_tex_src_ddy: {
         int deriv_cnt = instr->coord_components;
         if (instr->is_array)
            deriv_cnt--;
         LLVMValueRef deriv_vec[NIR_MAX_VEC_COMPONENTS] = { NULL };
         get_src_vec(bld, i, deriv_vec);
         for (unsigned chan = 0; chan < deriv_cnt; ++chan)
            derivs.ddy[chan] = deriv_vec[chan];
         for (unsigned chan = 0; chan < deriv_cnt; ++chan)
            derivs.ddy[chan] = cast_type(bld, derivs.ddy[chan], nir_type_float, 32);
         break;
      }
      case nir_tex_src_offset: {
         int offset_cnt = instr->coord_components;
         if (instr->is_array)
            offset_cnt--;
         LLVMValueRef offset_vec[NIR_MAX_VEC_COMPONENTS] = { NULL };
         get_src_vec(bld, i, offset_vec);
         for (unsigned chan = 0; chan < offset_cnt; ++chan) {
            offsets[chan] = offset_vec[chan];
            offsets[chan] = cast_type(bld, offsets[chan], nir_type_int, 32);
         }
         break;
      }
      case nir_tex_src_ms_index:
         ms_index = cast_type(bld, get_src(bld, &instr->src[i].src, 0), nir_type_int, 32);
         break;

      case nir_tex_src_texture_offset:
         texture_unit_offset = get_src(bld, &instr->src[i].src, 0);
         break;
      case nir_tex_src_sampler_offset:
         break;
      case nir_tex_src_texture_handle:
         texture_resource = get_src(bld, &instr->src[i].src, 0);
         break;
      case nir_tex_src_sampler_handle:
         sampler_resource = get_src(bld, &instr->src[i].src, 0);
         break;
      case nir_tex_src_plane:
         assert(nir_src_is_const(instr->src[i].src) && !nir_src_as_uint(instr->src[i].src));
         break;
      default:
         assert(0);
         break;
      }
   }
   if (!sampler_deref_instr)
      sampler_deref_instr = texture_deref_instr;

   if (!sampler_resource)
      sampler_resource = texture_resource;

   switch (instr->op) {
   case nir_texop_tex:
   case nir_texop_tg4:
   case nir_texop_txb:
   case nir_texop_txl:
   case nir_texop_txd:
   case nir_texop_lod:
      for (unsigned chan = 0; chan < 5; ++chan)
         coords[chan] = cast_type(bld, coords[chan], nir_type_float, 32);
      break;
   case nir_texop_txf:
   case nir_texop_txf_ms:
      for (unsigned chan = 0; chan < 5; ++chan)
         coords[chan] = cast_type(bld, coords[chan], nir_type_int, 32);
      break;
   default:
      ;
   }

   if (instr->is_array && instr->sampler_dim == GLSL_SAMPLER_DIM_1D) {
      /* move layer coord for 1d arrays. */
      coords[2] = coords[1];
      coords[1] = coord_zero;
   }

   uint32_t samp_base_index = 0, tex_base_index = 0;
   if (!sampler_deref_instr) {
      int samp_src_index = nir_tex_instr_src_index(instr, nir_tex_src_sampler_handle);
      if (samp_src_index == -1) {
         samp_base_index = instr->sampler_index;
      }
   }
   if (!texture_deref_instr) {
      int tex_src_index = nir_tex_instr_src_index(instr, nir_tex_src_texture_handle);
      if (tex_src_index == -1) {
         tex_base_index = instr->texture_index;
      }
   }

   if (instr->op == nir_texop_txd)
      params.derivs = &derivs;

   params.sample_key = lp_build_nir_sample_key(bld->shader->info.stage, instr);
   params.offsets = offsets;
   params.texture_index = tex_base_index;
   params.texture_index_offset = texture_unit_offset;
   params.sampler_index = samp_base_index;
   params.coords = coords;
   params.texel = texel;
   params.lod = explicit_lod;
   params.ms_index = ms_index;
   params.texture_resource = texture_resource;
   params.sampler_resource = sampler_resource;
   emit_tex(bld, &params);

   if (instr->def.bit_size != 32) {
      assert(instr->def.bit_size == 16);
      LLVMTypeRef vec_type = NULL;
      bool is_float = false;
      switch (nir_alu_type_get_base_type(instr->dest_type)) {
      case nir_type_float:
         is_float = true;
         break;
      case nir_type_int:
         vec_type = bld->int16_bld.vec_type;
         break;
      case nir_type_uint:
         vec_type = bld->uint16_bld.vec_type;
         break;
      default:
         UNREACHABLE("unexpected alu type");
      }
      for (int i = 0; i < instr->def.num_components; ++i) {
         if (is_float) {
            texel[i] = lp_build_float_to_half(gallivm, texel[i]);
         } else {
            texel[i] = LLVMBuildBitCast(builder, texel[i], bld->int_bld.vec_type, "");
            texel[i] = LLVMBuildTrunc(builder, texel[i], vec_type, "");
         }
      }
   }

   assign_ssa_dest(bld, &instr->def, texel);
}

static void
visit_ssa_undef(struct lp_build_nir_soa_context *bld,
                const nir_undef_instr *instr)
{
   unsigned num_components = instr->def.num_components;
   LLVMValueRef undef[NIR_MAX_VEC_COMPONENTS];
   struct lp_build_context *undef_bld = get_int_bld(bld, true,
                                                    instr->def.bit_size, false);
   for (unsigned i = 0; i < num_components; i++)
      undef[i] = LLVMGetUndef(undef_bld->vec_type);
   memset(&undef[num_components], 0, NIR_MAX_VEC_COMPONENTS - num_components);
   assign_ssa_dest(bld, &instr->def, undef);
}

static void
visit_jump(struct lp_build_nir_soa_context *bld,
           const nir_jump_instr *instr)
{
   switch (instr->type) {
   case nir_jump_break:
      break_stmt(bld);
      break;
   case nir_jump_continue:
      continue_stmt(bld);
      break;
   default:
      UNREACHABLE("Unknown jump instr\n");
   }
}

static void
visit_deref(struct lp_build_nir_soa_context *bld,
            nir_deref_instr *instr)
{
   if (!nir_deref_mode_is_one_of(instr, nir_var_mem_shared |
                                        nir_var_mem_global)) {
      return;
   }

   LLVMValueRef result[NIR_MAX_VEC_COMPONENTS];
   switch(instr->deref_type) {
   case nir_deref_type_var: {
      struct hash_entry *entry =
         _mesa_hash_table_search(bld->vars, instr->var);
      result[0] = entry->data;
      break;
   }
   default:
      UNREACHABLE("Unhandled deref_instr deref type");
   }

   assign_ssa_dest(bld, &instr->def, result);
}

static void
visit_call(struct lp_build_nir_soa_context *bld,
           nir_call_instr *instr)
{
   LLVMValueRef *args;
   struct hash_entry *entry = _mesa_hash_table_search(bld->fns, instr->callee);
   struct lp_build_fn *fn = entry->data;
   args = calloc(instr->num_params + LP_RESV_FUNC_ARGS, sizeof(LLVMValueRef));

   assert(args);

   args[0] = 0;
   for (unsigned i = 0; i < instr->num_params; i++) {
      LLVMValueRef arg[NIR_MAX_VEC_COMPONENTS];
      get_src_vec(bld, i, arg);
      for (uint32_t c = 0; c < nir_src_num_components(instr->params[i]); c++) {
         if (nir_src_bit_size(instr->params[i]) == 32 && LLVMTypeOf(arg[c]) == bld->base.vec_type)
            arg[c] = cast_type(bld, arg[c], nir_type_int, 32);
      }

      LLVMValueRef packed_arg = arg[0];
      if (nir_src_num_components(instr->params[i]) > 1)
         packed_arg = lp_build_gather_array(bld->base.gallivm, arg, nir_src_num_components(instr->params[i]));

      args[i + LP_RESV_FUNC_ARGS] = packed_arg;
   }

   emit_call(bld, fn, instr->num_params + LP_RESV_FUNC_ARGS, args);
   free(args);
}

static void
visit_block(struct lp_build_nir_soa_context *bld, nir_block *block)
{
   struct gallivm_state *gallivm = bld->base.gallivm;

   nir_foreach_instr(instr, block)
   {
      bld->instr = instr;

      if (gallivm->di_builder && gallivm->file_name && instr->has_debug_info) {
         nir_instr_debug_info *debug_info = nir_instr_get_debug_info(instr);
         LLVMMetadataRef di_loc = LLVMDIBuilderCreateDebugLocation(
            gallivm->context, debug_info->nir_line, 1, gallivm->di_function, NULL);
         LLVMSetCurrentDebugLocation2(gallivm->builder, di_loc);

         LLVMBuildStore(gallivm->builder, mask_vec(bld), bld->debug_exec_mask);
      }

      switch (instr->type) {
      case nir_instr_type_alu:
         visit_alu(bld, nir_instr_as_alu(instr));
         break;
      case nir_instr_type_load_const:
         visit_load_const(bld, nir_instr_as_load_const(instr));
         break;
      case nir_instr_type_intrinsic:
         visit_intrinsic(bld, nir_instr_as_intrinsic(instr));
         break;
      case nir_instr_type_tex:
         visit_tex(bld, nir_instr_as_tex(instr));
         break;
      case nir_instr_type_undef:
         visit_ssa_undef(bld, nir_instr_as_undef(instr));
         break;
      case nir_instr_type_jump:
         visit_jump(bld, nir_instr_as_jump(instr));
         break;
      case nir_instr_type_deref:
         visit_deref(bld, nir_instr_as_deref(instr));
         break;
      case nir_instr_type_call:
         visit_call(bld, nir_instr_as_call(instr));
         break;
      default:
         fprintf(stderr, "Unknown NIR instr type: ");
         nir_print_instr(instr, stderr);
         fprintf(stderr, "\n");
         abort();
      }
   }
}

static bool
lp_should_flatten_cf_list(struct exec_list *cf_list)
{
   if (!exec_list_is_singular(cf_list))
      return false;

   struct exec_node *head = exec_list_get_head(cf_list);
   nir_block *block = nir_cf_node_as_block(exec_node_data(nir_cf_node, head, node));
   return exec_list_length(&block->instr_list) < 8;
}

static void
visit_if(struct lp_build_nir_soa_context *bld, nir_if *if_stmt)
{
   LLVMValueRef cond = get_src(bld, &if_stmt->condition, 0);

   bool flatten_then = lp_should_flatten_cf_list(&if_stmt->then_list);
   bool flatten_else = lp_should_flatten_cf_list(&if_stmt->else_list);

   if_cond(bld, cond, flatten_then);
   visit_cf_list(bld, &if_stmt->then_list);
   else_stmt(bld, flatten_then, flatten_else);
   visit_cf_list(bld, &if_stmt->else_list);
   endif_stmt(bld, flatten_else);
}

static void
visit_loop(struct lp_build_nir_soa_context *bld, nir_loop *loop)
{
   assert(!nir_loop_has_continue_construct(loop));

   lp_exec_bgnloop(&bld->exec_mask, true);
   visit_cf_list(bld, &loop->body);
   lp_exec_endloop(bld->base.gallivm, &bld->exec_mask, bld->mask);
}

static void
visit_cf_list(struct lp_build_nir_soa_context *bld,
              struct exec_list *list)
{
   foreach_list_typed(nir_cf_node, node, node, list)
   {
      switch (node->type) {
      case nir_cf_node_block:
         visit_block(bld, nir_cf_node_as_block(node));
         break;
      case nir_cf_node_if:
         visit_if(bld, nir_cf_node_as_if(node));
         break;
      case nir_cf_node_loop:
         visit_loop(bld, nir_cf_node_as_loop(node));
         break;
      default:
         assert(0);
      }
   }
}

/* vector registers are stored as arrays in LLVM side,
   so we can use GEP on them, as to do exec mask stores
   we need to operate on a single components.
   arrays are:
   0.x, 1.x, 2.x, 3.x
   0.y, 1.y, 2.y, 3.y
   ....
*/
static LLVMTypeRef
get_register_type(struct lp_build_nir_soa_context *bld,
                  nir_intrinsic_instr *reg)
{
   unsigned num_array_elems = nir_intrinsic_num_array_elems(reg);
   unsigned bit_size = nir_intrinsic_bit_size(reg);
   unsigned num_components = nir_intrinsic_num_components(reg);

   struct lp_build_context *int_bld =
      get_int_bld(bld, true, bit_size == 1 ? 8 : bit_size, true);

   LLVMTypeRef type = int_bld->vec_type;
   if (num_components > 1)
      type = LLVMArrayType(type, num_components);
   if (num_array_elems)
      type = LLVMArrayType(type, num_array_elems);

   return type;
}

void lp_build_nir_soa_func(struct gallivm_state *gallivm,
                           struct nir_shader *shader,
                           nir_function_impl *impl,
                           const struct lp_build_tgsi_params *params,
                           LLVMValueRef (*outputs)[4])
{
   struct lp_build_nir_soa_context bld;
   const struct lp_type type = params->type;
   struct lp_type res_type;

   assert(type.length <= LP_MAX_VECTOR_LENGTH);
   memset(&res_type, 0, sizeof res_type);
   res_type.width = type.width;
   res_type.length = type.length;
   res_type.sign = 1;

   /* Setup build context */
   memset(&bld, 0, sizeof bld);
   lp_build_context_init(&bld.uint_bld, gallivm, lp_uint_type(type));
   lp_build_context_init(&bld.int_bld, gallivm, lp_int_type(type));
   {
      struct lp_type float_type = type;
      float_type.signed_zero_preserve =
         !!(shader->info.float_controls_execution_mode & FLOAT_CONTROLS_SIGNED_ZERO_PRESERVE_FP32);
      float_type.nan_preserve =
         !!(shader->info.float_controls_execution_mode & FLOAT_CONTROLS_NAN_PRESERVE_FP32);
      lp_build_context_init(&bld.base, gallivm, float_type);
   }
   {
      struct lp_type dbl_type;
      dbl_type = type;
      dbl_type.width *= 2;
      dbl_type.signed_zero_preserve =
         !!(shader->info.float_controls_execution_mode & FLOAT_CONTROLS_SIGNED_ZERO_PRESERVE_FP64);
      dbl_type.nan_preserve =
         !!(shader->info.float_controls_execution_mode & FLOAT_CONTROLS_NAN_PRESERVE_FP64);
      lp_build_context_init(&bld.dbl_bld, gallivm, dbl_type);
   }
   {
      struct lp_type half_type;
      half_type = type;
      half_type.width /= 2;
      half_type.signed_zero_preserve =
         !!(shader->info.float_controls_execution_mode & FLOAT_CONTROLS_SIGNED_ZERO_PRESERVE_FP16);
      half_type.nan_preserve =
         !!(shader->info.float_controls_execution_mode & FLOAT_CONTROLS_NAN_PRESERVE_FP16);
      lp_build_context_init(&bld.half_bld, gallivm, half_type);
   }
   {
      struct lp_type uint64_type;
      uint64_type = lp_uint_type(type);
      uint64_type.width *= 2;
      lp_build_context_init(&bld.uint64_bld, gallivm, uint64_type);
   }
   {
      struct lp_type int64_type;
      int64_type = lp_int_type(type);
      int64_type.width *= 2;
      lp_build_context_init(&bld.int64_bld, gallivm, int64_type);
   }
   {
      struct lp_type uint16_type;
      uint16_type = lp_uint_type(type);
      uint16_type.width /= 2;
      lp_build_context_init(&bld.uint16_bld, gallivm, uint16_type);
   }
   {
      struct lp_type int16_type;
      int16_type = lp_int_type(type);
      int16_type.width /= 2;
      lp_build_context_init(&bld.int16_bld, gallivm, int16_type);
   }
   {
      struct lp_type uint8_type;
      uint8_type = lp_uint_type(type);
      uint8_type.width /= 4;
      lp_build_context_init(&bld.uint8_bld, gallivm, uint8_type);
   }
   {
      struct lp_type int8_type;
      int8_type = lp_int_type(type);
      int8_type.width /= 4;
      lp_build_context_init(&bld.int8_bld, gallivm, int8_type);
   }
   {
      struct lp_type bool_type;
      bool_type = lp_int_type(type);
      bool_type.width /= 32;
      lp_build_context_init(&bld.bool_bld, gallivm, bool_type);
   }

   /* Scalar builders */
   struct lp_type elem_type = lp_elem_type(type);
   lp_build_context_init(&bld.scalar_uint_bld, gallivm, lp_uint_type(elem_type));
   lp_build_context_init(&bld.scalar_int_bld, gallivm, lp_int_type(elem_type));
   {
      struct lp_type float_type = elem_type;
      float_type.signed_zero_preserve =
         !!(shader->info.float_controls_execution_mode & FLOAT_CONTROLS_SIGNED_ZERO_PRESERVE_FP32);
      float_type.nan_preserve =
         !!(shader->info.float_controls_execution_mode & FLOAT_CONTROLS_NAN_PRESERVE_FP32);
      lp_build_context_init(&bld.scalar_base, gallivm, float_type);
   }
   {
      struct lp_type dbl_type;
      dbl_type = elem_type;
      dbl_type.width *= 2;
      dbl_type.signed_zero_preserve =
         !!(shader->info.float_controls_execution_mode & FLOAT_CONTROLS_SIGNED_ZERO_PRESERVE_FP64);
      dbl_type.nan_preserve =
         !!(shader->info.float_controls_execution_mode & FLOAT_CONTROLS_NAN_PRESERVE_FP64);
      lp_build_context_init(&bld.scalar_dbl_bld, gallivm, dbl_type);
   }
   {
      struct lp_type half_type;
      half_type = elem_type;
      half_type.width /= 2;
      half_type.signed_zero_preserve =
         !!(shader->info.float_controls_execution_mode & FLOAT_CONTROLS_SIGNED_ZERO_PRESERVE_FP16);
      half_type.nan_preserve =
         !!(shader->info.float_controls_execution_mode & FLOAT_CONTROLS_NAN_PRESERVE_FP16);
      lp_build_context_init(&bld.scalar_half_bld, gallivm, half_type);
   }
   {
      struct lp_type uint64_type;
      uint64_type = lp_uint_type(elem_type);
      uint64_type.width *= 2;
      lp_build_context_init(&bld.scalar_uint64_bld, gallivm, uint64_type);
   }
   {
      struct lp_type int64_type;
      int64_type = lp_int_type(elem_type);
      int64_type.width *= 2;
      lp_build_context_init(&bld.scalar_int64_bld, gallivm, int64_type);
   }
   {
      struct lp_type uint16_type;
      uint16_type = lp_uint_type(elem_type);
      uint16_type.width /= 2;
      lp_build_context_init(&bld.scalar_uint16_bld, gallivm, uint16_type);
   }
   {
      struct lp_type int16_type;
      int16_type = lp_int_type(elem_type);
      int16_type.width /= 2;
      lp_build_context_init(&bld.scalar_int16_bld, gallivm, int16_type);
   }
   {
      struct lp_type uint8_type;
      uint8_type = lp_uint_type(elem_type);
      uint8_type.width /= 4;
      lp_build_context_init(&bld.scalar_uint8_bld, gallivm, uint8_type);
   }
   {
      struct lp_type int8_type;
      int8_type = lp_int_type(elem_type);
      int8_type.width /= 4;
      lp_build_context_init(&bld.scalar_int8_bld, gallivm, int8_type);
   }
   {
      struct lp_type bool_type;
      bool_type = lp_int_type(elem_type);
      bool_type.width /= 32;
      lp_build_context_init(&bld.scalar_bool_bld, gallivm, bool_type);
   }

   bld.fns = params->fns;
   bld.func = params->current_func;
   bld.mask = params->mask;
   bld.inputs = params->inputs;
   bld.outputs = outputs;
   bld.consts_ptr = params->consts_ptr;
   bld.ssbo_ptr = params->ssbo_ptr;
   bld.sampler = params->sampler;

   bld.context_type = params->context_type;
   bld.context_ptr = params->context_ptr;
   bld.resources_type = params->resources_type;
   bld.resources_ptr = params->resources_ptr;
   bld.thread_data_type = params->thread_data_type;
   bld.thread_data_ptr = params->thread_data_ptr;
   bld.image = params->image;
   bld.shared_ptr = params->shared_ptr;
   bld.shared_size = params->shared_size;
   bld.payload_ptr = params->payload_ptr;
   bld.coro = params->coro;
   bld.num_inputs = params->num_inputs;
   bld.indirects = 0;
   if (shader->info.inputs_read_indirectly)
      bld.indirects |= nir_var_shader_in;

   bld.gs_iface = params->gs_iface;
   bld.tcs_iface = params->tcs_iface;
   bld.tes_iface = params->tes_iface;
   bld.fs_iface = params->fs_iface;
   bld.mesh_iface = params->mesh_iface;
   if (bld.gs_iface) {
      struct lp_build_context *uint_bld = &bld.uint_bld;

      bld.gs_vertex_streams = params->gs_vertex_streams;
      bld.max_output_vertices_vec = lp_build_const_int_vec(gallivm, bld.int_bld.type,
                                                           shader->info.gs.vertices_out);
      for (int i = 0; i < params->gs_vertex_streams; i++) {
         bld.emitted_prims_vec_ptr[i] =
            lp_build_alloca(gallivm, uint_bld->vec_type, "emitted_prims_ptr");
         bld.emitted_vertices_vec_ptr[i] =
            lp_build_alloca(gallivm, uint_bld->vec_type, "emitted_vertices_ptr");
         bld.total_emitted_vertices_vec_ptr[i] =
            lp_build_alloca(gallivm, uint_bld->vec_type, "total_emitted_vertices_ptr");
      }
   }
   lp_exec_mask_init(&bld.exec_mask, &bld.int_bld);

   if (params->system_values)
      bld.system_values = *params->system_values;

   bld.shader = shader;

   bld.scratch_size = align(shader->scratch_size, 8);
   if (params->scratch_ptr)
      bld.scratch_ptr = params->scratch_ptr;
   else if (shader->scratch_size) {
      bld.scratch_ptr = lp_build_array_alloca(gallivm,
                                              LLVMInt8TypeInContext(gallivm->context),
                                              lp_build_const_int32(gallivm, bld.scratch_size * type.length),
                                              "scratch");
   }

   if (!exec_list_is_singular(&shader->functions)) {
      bld.call_context_type = lp_build_cs_func_call_context(gallivm, type.length, bld.context_type, bld.resources_type);
      if (!params->call_context_ptr) {
         build_call_context(&bld);
      } else
         bld.call_context_ptr = params->call_context_ptr;
   }

   bld.null_qword_ptr = lp_build_alloca(gallivm, bld.uint64_bld.elem_type, "null_qword_ptr");
   bld.noop_store_ptr = lp_build_alloca_undef(gallivm, bld.uint64_bld.elem_type, "noop_store_ptr");

   emit_prologue(&bld);

   nir_foreach_shader_out_variable(variable, shader)
      emit_var_decl(&bld, variable);

   if (shader->info.io_lowered) {
      uint64_t outputs_written = shader->info.outputs_written;

      while (outputs_written) {
         unsigned location = u_bit_scan64(&outputs_written);
         nir_variable var = {0};

         var.type = glsl_vec4_type();
         var.data.mode = nir_var_shader_out;
         var.data.location = location;
         var.data.driver_location = util_bitcount64(shader->info.outputs_written &
                                                    BITFIELD64_MASK(location));
         emit_var_decl(&bld, &var);
      }
   }

   bld.regs = _mesa_pointer_hash_table_create(NULL);
   bld.vars = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                      _mesa_key_pointer_equal);
   bld.range_ht = _mesa_pointer_hash_table_create(NULL);

   nir_index_ssa_defs(impl);

   if (bld.base.gallivm->di_builder && bld.base.gallivm->file_name && shader->has_debug_info) {
      char *shader_src = nir_shader_gather_debug_info(shader, bld.base.gallivm->file_name, 1);
      if (shader_src) {
         FILE *f = fopen(bld.base.gallivm->file_name, "w");
         fprintf(f, "%s\n", shader_src);
         fclose(f);

         ralloc_free(shader_src);
      }

      LLVMValueRef exec_mask = mask_vec(&bld);
      bld.debug_exec_mask = lp_build_alloca_undef(gallivm, LLVMTypeOf(exec_mask), "exec_mask");
      LLVMBuildStore(gallivm->builder, exec_mask, bld.debug_exec_mask);

      LLVMMetadataRef di_type = lp_bld_debug_info_type(gallivm, LLVMTypeOf(exec_mask));
      LLVMMetadataRef di_var = LLVMDIBuilderCreateAutoVariable(
         gallivm->di_builder, gallivm->di_function, "exec_mask", strlen("exec_mask"),
         gallivm->file, 0, di_type, true, LLVMDIFlagZero, 0);

      LLVMMetadataRef di_expr = LLVMDIBuilderCreateExpression(gallivm->di_builder, NULL, 0);

      LLVMMetadataRef di_loc = LLVMDIBuilderCreateDebugLocation(
         gallivm->context, 0, 0, gallivm->di_function, NULL);

#if LLVM_VERSION_MAJOR >= 19
      LLVMDIBuilderInsertDeclareRecordAtEnd(gallivm->di_builder, bld.debug_exec_mask, di_var, di_expr, di_loc,
                                            LLVMGetInsertBlock(gallivm->builder));
#else
      LLVMDIBuilderInsertDeclareAtEnd(gallivm->di_builder, bld.debug_exec_mask, di_var, di_expr, di_loc,
                                      LLVMGetInsertBlock(gallivm->builder));
#endif
   }

   nir_foreach_reg_decl(reg, impl) {
      LLVMTypeRef type = get_register_type(&bld, reg);
      LLVMValueRef reg_alloc = lp_build_alloca(bld.base.gallivm,
                                               type, "reg");
      _mesa_hash_table_insert(bld.regs, reg, reg_alloc);
   }

   nir_divergence_analysis_impl(impl, impl->function->shader->options->divergence_analysis_options);
   bld.ssa_defs = calloc(impl->ssa_alloc * NIR_MAX_VEC_COMPONENTS * 2, sizeof(LLVMValueRef));
   visit_cf_list(&bld, &impl->body);

   free(bld.ssa_defs);
   ralloc_free(bld.vars);
   ralloc_free(bld.regs);
   ralloc_free(bld.range_ht);

   if (bld.gs_iface) {
      LLVMBuilderRef builder = bld.base.gallivm->builder;
      LLVMValueRef total_emitted_vertices_vec;
      LLVMValueRef emitted_prims_vec;

      for (int i = 0; i < params->gs_vertex_streams; i++) {
         end_primitive_masked(&bld, lp_build_mask_value(bld.mask), i);

         total_emitted_vertices_vec =
            LLVMBuildLoad2(builder, bld.uint_bld.vec_type, bld.total_emitted_vertices_vec_ptr[i], "");

         emitted_prims_vec =
            LLVMBuildLoad2(builder, bld.uint_bld.vec_type, bld.emitted_prims_vec_ptr[i], "");
         bld.gs_iface->gs_epilogue(bld.gs_iface,
                                   total_emitted_vertices_vec,
                                   emitted_prims_vec, i);
      }
   }
   lp_exec_mask_fini(&bld.exec_mask);
}

void
lp_build_nir_soa_prepasses(struct nir_shader *nir)
{
   lp_build_opt_nir(nir);

   NIR_PASS(_, nir, nir_lower_vars_to_ssa);
   NIR_PASS(_, nir, nir_remove_dead_derefs);
   NIR_PASS(_, nir, nir_remove_dead_variables, nir_var_function_temp, NULL);

   NIR_PASS(_, nir, nir_lower_load_const_to_scalar);

   NIR_PASS(_, nir, nir_convert_to_lcssa, false, false);
   NIR_PASS(_, nir, nir_lower_all_phis_to_scalar);

   bool progress;
   do {
      progress = false;
      NIR_PASS(progress, nir, nir_lower_alu_to_scalar, NULL, NULL);
      NIR_PASS(progress, nir, nir_opt_copy_prop);
      NIR_PASS(progress, nir, nir_opt_dce);
   } while (progress);

   nir_divergence_analysis(nir);

   /* Do nort use NIR_PASS after running divergence analysis to make sure
    * that divergence metadata is preserved.
    */
   nir_convert_from_ssa(nir, true, true);
   nir_lower_locals_to_regs(nir, 1);

   nir_opt_dce(nir);
}

void lp_build_nir_soa(struct gallivm_state *gallivm,
                      struct nir_shader *shader,
                      const struct lp_build_tgsi_params *params,
                      LLVMValueRef (*outputs)[4])
{
   lp_build_nir_soa_prepasses(shader);
   lp_build_nir_soa_func(gallivm, shader,
                         nir_shader_get_entrypoint(shader),
                         params, outputs);
}
