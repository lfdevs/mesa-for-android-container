/**************************************************************************
 *
 * Copyright 2022 Red Hat
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
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 **************************************************************************/

#include "lp_bld_nir.h"
#include "lp_bld_arit.h"
#include "lp_bld_conv.h"
#include "lp_bld_init.h"
#include "lp_bld_const.h"
#include "lp_bld_flow.h"
#include "lp_bld_quad.h"
#include "lp_bld_sample.h"
#include "lp_bld_swizzle.h"
#include "lp_bld_tgsi.h"
#include "lp_bld_debug.h"
#include "util/u_math.h"


struct lp_build_nir_aos_context
{
   struct lp_build_context base;
   struct lp_build_context uint_bld;
   struct lp_build_context int_bld;

   LLVMValueRef *ssa_defs;
   struct hash_table *regs;

   nir_shader *shader;

   /*
    * AoS swizzle used:
    * - swizzles[0] = red index
    * - swizzles[1] = green index
    * - swizzles[2] = blue index
    * - swizzles[3] = alpha index
    */
   unsigned char swizzles[4];
   unsigned char inv_swizzles[4];

   LLVMValueRef consts_ptr;
   const LLVMValueRef *inputs;
   LLVMValueRef *outputs;

   const struct lp_build_sampler_aos *sampler;
};

static inline struct lp_build_context *
get_flt_bld(struct lp_build_nir_aos_context *bld,
            unsigned op_bit_size)
{
   switch (op_bit_size) {
   default:
   case 32:
      return &bld->base;
   }
}

static inline struct lp_build_context *
get_int_bld(struct lp_build_nir_aos_context *bld,
            bool is_unsigned,
            unsigned op_bit_size)
{
   if (is_unsigned) {
      switch (op_bit_size) {
      case 32:
      default:
         return &bld->uint_bld;
      }
   } else {
      switch (op_bit_size) {
      default:
      case 32:
         return &bld->int_bld;
      }
   }
}

static LLVMValueRef
swizzle_aos(struct lp_build_nir_aos_context *bld,
            LLVMValueRef a,
            unsigned swizzle_x,
            unsigned swizzle_y,
            unsigned swizzle_z,
            unsigned swizzle_w)
{
   unsigned char swizzles[4];
   assert(swizzle_x < 4);
   assert(swizzle_y < 4);
   assert(swizzle_z < 4);
   assert(swizzle_w < 4);

   swizzles[bld->inv_swizzles[0]] = bld->swizzles[swizzle_x];
   swizzles[bld->inv_swizzles[1]] = bld->swizzles[swizzle_y];
   swizzles[bld->inv_swizzles[2]] = bld->swizzles[swizzle_z];
   swizzles[bld->inv_swizzles[3]] = bld->swizzles[swizzle_w];

   return lp_build_swizzle_aos(&bld->base, a, swizzles);
}

static void
emit_var_decl(struct lp_build_nir_aos_context *bld,
              nir_variable *var)
{
   if (!bld->outputs || var->data.mode != nir_var_shader_out)
      return;

   unsigned this_loc = var->data.driver_location;
   bld->outputs[this_loc] = lp_build_alloca(bld->base.gallivm,
                                            bld->base.vec_type,
                                            "output");
}


static void
emit_load_var(struct lp_build_nir_aos_context *bld,
              nir_variable_mode deref_mode,
              nir_variable *var,
              LLVMValueRef result[NIR_MAX_VEC_COMPONENTS])
{
   unsigned location = var->data.driver_location;

   if (deref_mode == nir_var_shader_in) {
      result[0] = bld->inputs[location];
   }
}


static void
emit_store_var(struct lp_build_nir_aos_context *bld,
               nir_variable_mode deref_mode,
               nir_variable *var,
               LLVMValueRef vals)
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   unsigned location = var->data.driver_location;

   if (deref_mode == nir_var_shader_out) {
      LLVMBuildStore(gallivm->builder, vals, bld->outputs[location]);
   }
}


static LLVMValueRef
emit_load_reg(struct lp_build_nir_aos_context *bld,
              struct lp_build_context *reg_bld,
              LLVMValueRef reg_storage)
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   return LLVMBuildLoad2(gallivm->builder, reg_bld->vec_type, reg_storage, "");
}

/*
 * If an instruction has a writemask like r0.x = foo and the
 * AOS/linear context uses swizzle={2,1,0,3} we need to change
 * the writemask to r0.z
 */
static unsigned
swizzle_writemask(struct lp_build_nir_aos_context *bld,
                  unsigned writemask)
{
   assert(writemask != 0x0);
   assert(writemask != 0xf);

   // Ex: swap r/b channels
   unsigned new_writemask = 0;
   for (unsigned chan = 0; chan < 4; chan++) {
      if (writemask & (1 << chan)) {
         new_writemask |= 1 << bld->swizzles[chan];
      }
   }
   return new_writemask;
}


static void
emit_store_reg(struct lp_build_nir_aos_context *bld,
               struct lp_build_context *reg_bld,
               unsigned writemask,
               LLVMValueRef reg_storage,
               LLVMValueRef vals[NIR_MAX_VEC_COMPONENTS])
{
   struct gallivm_state *gallivm = bld->base.gallivm;

   if (writemask == 0xf) {
      LLVMBuildStore(gallivm->builder, vals[0], reg_storage);
      return;
   }

   writemask = swizzle_writemask(bld, writemask);

   LLVMValueRef cur = LLVMBuildLoad2(gallivm->builder, reg_bld->vec_type,
                                     reg_storage, "");
   LLVMTypeRef i32t = LLVMInt32TypeInContext(gallivm->context);
   LLVMValueRef shuffles[LP_MAX_VECTOR_LENGTH];
   for (unsigned j = 0; j < 16; j++) {
      unsigned comp = j % 4;
      if (writemask & (1 << comp)) {
         shuffles[j] = LLVMConstInt(i32t, 16 + j, 0); // new val
      } else {
         shuffles[j] = LLVMConstInt(i32t, j, 0);      // cur val
      }
   }
   cur = LLVMBuildShuffleVector(gallivm->builder, cur, vals[0],
                                LLVMConstVector(shuffles, 16), "");

   LLVMBuildStore(gallivm->builder, cur, reg_storage);
}


static void
emit_load_ubo(struct lp_build_nir_aos_context *bld,
              unsigned nc,
              LLVMValueRef offset,
              LLVMValueRef result[NIR_MAX_VEC_COMPONENTS])
{
   LLVMBuilderRef builder = bld->base.gallivm->builder;
   struct gallivm_state *gallivm = bld->base.gallivm;
   struct lp_type type = bld->base.type;
   LLVMValueRef res;

   res = bld->base.undef;
   offset = LLVMBuildExtractElement(builder, offset,
                                    lp_build_const_int32(gallivm, 0), "");
   assert(LLVMIsConstant(offset));
   unsigned offset_val = LLVMConstIntGetZExtValue(offset) >> 2;
   for (unsigned chan = 0; chan < nc; ++chan) {
      LLVMValueRef this_offset = lp_build_const_int32(gallivm,
                                                      offset_val + chan);

      LLVMTypeRef scalar_type = LLVMInt8TypeInContext(gallivm->context);
      LLVMValueRef scalar_ptr = LLVMBuildGEP2(builder, scalar_type, bld->consts_ptr, &this_offset, 1, "");
      LLVMValueRef scalar = LLVMBuildLoad2(builder, scalar_type, scalar_ptr, "");

      lp_build_name(scalar, "const[%u].%c", offset_val, "xyzw"[chan]);

      LLVMValueRef swizzle = lp_build_const_int32(bld->base.gallivm,
                                           nc == 1 ? 0 : bld->swizzles[chan]);

      res = LLVMBuildInsertElement(builder, res, scalar, swizzle, "");
   }

   if (type.length > 4) {
      LLVMValueRef shuffles[LP_MAX_VECTOR_LENGTH];

      for (unsigned chan = 0; chan < nc; ++chan) {
         shuffles[chan] =
            lp_build_const_int32(bld->base.gallivm, chan);
      }

      for (unsigned i = nc; i < type.length; ++i) {
         shuffles[i] = shuffles[i % nc];
      }

      res = LLVMBuildShuffleVector(builder, res, bld->base.undef,
                                   LLVMConstVector(shuffles, type.length),
                                   "");
   }

   if (nc == 4)
      swizzle_aos(bld, res, 0, 1, 2, 3);

   result[0] = res;
}


static void
emit_tex(struct lp_build_nir_aos_context *bld,
         struct lp_sampler_params *params)
{
   static const struct lp_derivatives derivs = { 0 };
   params->type = bld->base.type;
   params->texel[0] = bld->sampler->emit_fetch_texel(bld->sampler,
                                                     &bld->base,
                                                     TGSI_TEXTURE_2D,
                                                     params->texture_index,
                                                     params->coords[0],
                                                     params->derivs ? params->derivs[0] : derivs,
                                                     LP_BLD_TEX_MODIFIER_NONE);
}


static void
emit_load_const(struct lp_build_nir_aos_context *bld,
                const nir_load_const_instr *instr,
                LLVMValueRef outval[NIR_MAX_VEC_COMPONENTS])
{
   LLVMValueRef elems[16];
   const int nc = instr->def.num_components;
   bool do_swizzle = false;

   if (nc == 4)
      do_swizzle = true;

   /* The constant is something like {float, float, float, float}.
    * We need to convert the float values from [0,1] to ubyte in [0,255].
    * We previously checked for values outside [0,1] in
    * llvmpipe_nir_fn_is_linear_compat().
    * Also, we convert the (typically) 4-element float constant into a
    * swizzled 16-element ubyte constant (z,y,x,w, z,y,x,w, z,y,x,w, z,y,x,w)
    * since that's what 'linear' mode operates on.
    */
   assert(bld->base.type.length <= ARRAY_SIZE(elems));
   for (unsigned i = 0; i < bld->base.type.length; i++) {
      const unsigned j = do_swizzle ? bld->swizzles[i % nc] : i % nc;
      assert(instr->value[j].f32 >= 0.0f);
      assert(instr->value[j].f32 <= 1.0f);
      const unsigned u8val = float_to_ubyte(instr->value[j].f32);
      elems[i] = LLVMConstInt(bld->uint_bld.int_elem_type, u8val, 0);
   }
   outval[0] = LLVMConstVector(elems, bld->base.type.length);
   outval[1] = outval[2] = outval[3] = NULL;
}

static LLVMValueRef
cast_type(struct lp_build_nir_aos_context *bld, LLVMValueRef val,
          nir_alu_type alu_type, unsigned bit_size)
{
   LLVMBuilderRef builder = bld->base.gallivm->builder;
   switch (alu_type) {
   case nir_type_float:
      switch (bit_size) {
      case 32:
         return LLVMBuildBitCast(builder, val, bld->base.vec_type, "");
      default:
         assert(0);
         break;
      }
      break;
   case nir_type_int:
      switch (bit_size) {
      case 32:
         return LLVMBuildBitCast(builder, val, bld->int_bld.vec_type, "");
      default:
         assert(0);
         break;
      }
      break;
   case nir_type_uint:
      switch (bit_size) {
      case 1:
      case 32:
         return LLVMBuildBitCast(builder, val, bld->uint_bld.vec_type, "");
      default:
         assert(0);
         break;
      }
      break;
   case nir_type_uint32:
      return LLVMBuildBitCast(builder, val, bld->uint_bld.vec_type, "");
   default:
      return val;
   }
   return NULL;
}

static LLVMValueRef
get_src(struct lp_build_nir_aos_context *bld, nir_src src)
{
   return bld->ssa_defs[src.ssa->index];
}

static void
assign_ssa(struct lp_build_nir_aos_context *bld, int idx, LLVMValueRef ptr)
{
   bld->ssa_defs[idx] = ptr;
}

static void
assign_ssa_dest(struct lp_build_nir_aos_context *bld, const nir_def *ssa,
                LLVMValueRef *vals)
{
   assign_ssa(bld, ssa->index, vals[0]);
}

/**
 * Get a source register value for an ALU instruction.
 * This is where swizzles are handled.  There should be no negation
 * or absolute value modifiers.
 * num_components indicates the number of components needed in the
 * returned array or vector.
 */
static LLVMValueRef
get_alu_src(struct lp_build_nir_aos_context *bld,
            nir_alu_src src,
            unsigned num_components)
{
   assert(num_components >= 1);
   assert(num_components <= 4);

   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;
   const unsigned src_components = nir_src_num_components(src.src);
   assert(src_components > 0);
   LLVMValueRef value = get_src(bld, src.src);
   assert(value);

   /* check if swizzling needed for the src vector */
   bool need_swizzle = false;
   for (unsigned i = 0; i < src_components; ++i) {
      if (src.swizzle[i] != i) {
         need_swizzle = true;
         break;
      }
   }

   if (!need_swizzle)
      return value;

   if (need_swizzle || num_components != src_components) {
      if (need_swizzle) {
         // Handle swizzle for AOS
         assert(LLVMGetTypeKind(LLVMTypeOf(value)) == LLVMVectorTypeKind);

         // swizzle vector of ((r,g,b,a), (r,g,b,a), (r,g,b,a), (r,g,b,a))
         assert(bld->base.type.width == 8);
         assert(bld->base.type.length == 16);

         // Do our own swizzle here since lp_build_swizzle_aos_n() does
         // not do what we want.
         // Ex: value = {r0,g0,b0,a0, r1,g1,b1,a1, r2,g2,b2,a2, r3,g3,b3,a3}.
         // aos swizzle = {2,1,0,3}  // swap red/blue
         // shuffles = {2,1,0,3, 6,5,4,7, 10,9,8,11, 14,13,12,15}
         // result = {b0,g0,r0,a0, b1,g1,r1,a1, b2,g2,r2,a2, b3,g3,r3,a3}.
         LLVMValueRef shuffles[LP_MAX_VECTOR_WIDTH];
         for (unsigned i = 0; i < 16; i++) {
            unsigned chan = i % 4;
            /* apply src register swizzle */
            if (chan < num_components) {
               chan = src.swizzle[chan];
            } else {
               chan = src.swizzle[0];
            }
            /* apply aos swizzle */
            chan = bld->swizzles[chan];
            shuffles[i] = lp_build_const_int32(gallivm, (i & ~3) + chan);
         }
         value = LLVMBuildShuffleVector(builder, value,
                                        LLVMGetUndef(LLVMTypeOf(value)),
                                        LLVMConstVector(shuffles, 16), "");
      } else if (src_components > 1 && num_components == 1) {
         value = LLVMBuildExtractValue(gallivm->builder, value,
                                       src.swizzle[0], "");
      } else if (src_components == 1 && num_components > 1) {
         LLVMValueRef values[] = {value, value, value, value,
                                  value, value, value, value,
                                  value, value, value, value,
                                  value, value, value, value};
         value = lp_nir_array_build_gather_values(builder, values, num_components);
      } else {
         LLVMValueRef arr = LLVMGetUndef(LLVMArrayType(LLVMTypeOf(LLVMBuildExtractValue(builder, value, 0, "")), num_components));
         for (unsigned i = 0; i < num_components; i++)
            arr = LLVMBuildInsertValue(builder, arr, LLVMBuildExtractValue(builder, value, src.swizzle[i], ""), i, "");
         value = arr;
      }
   }

   return value;
}

static LLVMValueRef
do_alu_action(struct lp_build_nir_aos_context *bld,
              const nir_alu_instr *instr,
              unsigned src_bit_size[NIR_MAX_VEC_COMPONENTS],
              LLVMValueRef src[NIR_MAX_VEC_COMPONENTS])
{
   LLVMValueRef result = NULL;

   switch (instr->op) {
   case nir_op_fmul:
      result = lp_build_mul(get_flt_bld(bld, src_bit_size[0]),
                            src[0], src[1]);
      break;
   case nir_op_mov:
      result = src[0];
      break;
   default:
      assert(0);
      break;
   }
   return result;
}


static void
visit_alu(struct lp_build_nir_aos_context *bld,
          const nir_alu_instr *instr)
{
   LLVMValueRef src[NIR_MAX_VEC_COMPONENTS];
   unsigned src_bit_size[NIR_MAX_VEC_COMPONENTS];
   const unsigned num_components = instr->def.num_components;
   unsigned src_components;

   switch (instr->op) {
   case nir_op_vec2:
   case nir_op_vec4:
      src_components = 1;
      break;
   default:
      src_components = num_components;
      break;
   }

   for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
      src[i] = get_alu_src(bld, instr->src[i], src_components);
      src_bit_size[i] = nir_src_bit_size(instr->src[i].src);
   }

   LLVMValueRef result[NIR_MAX_VEC_COMPONENTS] = { NULL };
   if (instr->op == nir_op_vec4 ||
       instr->op == nir_op_vec2) {
      for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
         result[i] = cast_type(bld, src[i],
                               nir_op_infos[instr->op].input_types[i],
                               src_bit_size[i]);
      }
   } else {
      result[0] = do_alu_action(bld, instr, src_bit_size, src);
   }

   assign_ssa_dest(bld, &instr->def, result);
}

static void
visit_load_const(struct lp_build_nir_aos_context *bld,
                 const nir_load_const_instr *instr)
{
   LLVMValueRef result[NIR_MAX_VEC_COMPONENTS] = { NULL };
   emit_load_const(bld, instr, result);
   assign_ssa_dest(bld, &instr->def, result);
}

static void
visit_load_input(struct lp_build_nir_aos_context *bld,
                 nir_intrinsic_instr *instr,
                 LLVMValueRef result[NIR_MAX_VEC_COMPONENTS])
{
   nir_variable var = {0};
   var.data.location = nir_intrinsic_io_semantics(instr).location;
   var.data.driver_location = nir_intrinsic_base(instr);
   var.data.location_frac = nir_intrinsic_component(instr);

   emit_load_var(bld, nir_var_shader_in, &var, result);
}

static void
visit_store_output(struct lp_build_nir_aos_context *bld,
                   nir_intrinsic_instr *instr)
{
   nir_variable var = {0};
   var.data.location = nir_intrinsic_io_semantics(instr).location;
   var.data.driver_location = nir_intrinsic_base(instr);
   var.data.location_frac = nir_intrinsic_component(instr);

   unsigned mask = nir_intrinsic_write_mask(instr);

   LLVMValueRef src = get_src(bld, instr->src[0]);

   if (mask == 0x1 && LLVMGetTypeKind(LLVMTypeOf(src)) == LLVMArrayTypeKind) {
      src = LLVMBuildExtractValue(bld->base.gallivm->builder,
                                  src, 0, "");
   }

   emit_store_var(bld, nir_var_shader_out, &var, src);
}

static void
visit_load_reg(struct lp_build_nir_aos_context *bld,
               nir_intrinsic_instr *instr,
               LLVMValueRef result[NIR_MAX_VEC_COMPONENTS])
{
   nir_intrinsic_instr *decl = nir_reg_get_decl(instr->src[0].ssa);

   struct hash_entry *entry = _mesa_hash_table_search(bld->regs, decl);
   LLVMValueRef reg_storage = (LLVMValueRef)entry->data;

   unsigned bit_size = nir_intrinsic_bit_size(decl);
   struct lp_build_context *reg_bld = get_int_bld(bld, true, bit_size);

   LLVMValueRef val = emit_load_reg(bld, reg_bld, reg_storage);

   result[0] = val;
}

static void
visit_store_reg(struct lp_build_nir_aos_context *bld,
                nir_intrinsic_instr *instr)
{
   nir_intrinsic_instr *decl = nir_reg_get_decl(instr->src[1].ssa);
   unsigned write_mask = nir_intrinsic_write_mask(instr);
   assert(write_mask != 0x0);

   LLVMValueRef val = get_src(bld, instr->src[0]);
   LLVMValueRef vals[NIR_MAX_VEC_COMPONENTS] = { NULL };
   vals[0] = val;

   struct hash_entry *entry = _mesa_hash_table_search(bld->regs, decl);
   LLVMValueRef reg_storage = (LLVMValueRef)entry->data;

   unsigned bit_size = nir_intrinsic_bit_size(decl);
   struct lp_build_context *reg_bld = get_int_bld(bld, true, bit_size);

   emit_store_reg(bld, reg_bld, write_mask, reg_storage, vals);
}

static void
visit_load_var(struct lp_build_nir_aos_context *bld,
               nir_intrinsic_instr *instr,
               LLVMValueRef result[NIR_MAX_VEC_COMPONENTS])
{
   nir_deref_instr *deref = nir_def_as_deref(instr->src[0].ssa);
   nir_variable *var = nir_deref_instr_get_variable(deref);
   assert(util_bitcount(deref->modes) == 1);
   nir_variable_mode mode = deref->modes;

   emit_load_var(bld, mode, var, result);
}

static void
visit_store_var(struct lp_build_nir_aos_context *bld,
                nir_intrinsic_instr *instr)
{
   nir_deref_instr *deref = nir_def_as_deref(instr->src[0].ssa);
   nir_variable *var = nir_deref_instr_get_variable(deref);
   assert(util_bitcount(deref->modes) == 1);
   nir_variable_mode mode = deref->modes;
   LLVMValueRef src = get_src(bld, instr->src[1]);

   emit_store_var(bld, mode, var, src);
}

static void
visit_load_ubo(struct lp_build_nir_aos_context *bld,
               nir_intrinsic_instr *instr,
               LLVMValueRef result[NIR_MAX_VEC_COMPONENTS])
{
   LLVMValueRef offset = get_src(bld, instr->src[1]);
   emit_load_ubo(bld, instr->def.num_components, offset, result);
}

static void
visit_intrinsic(struct lp_build_nir_aos_context *bld,
                nir_intrinsic_instr *instr)
{
   LLVMValueRef result[NIR_MAX_VEC_COMPONENTS] = { NULL };
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
visit_tex(struct lp_build_nir_aos_context *bld, nir_tex_instr *instr)
{
   struct gallivm_state *gallivm = bld->base.gallivm;
   LLVMBuilderRef builder = gallivm->builder;

   nir_deref_instr *texture_deref_instr = NULL;
   nir_deref_instr *sampler_deref_instr = NULL;

   LLVMValueRef coords[5] = { NULL };
   struct lp_sampler_params params = { 0 };
   LLVMValueRef texel[NIR_MAX_VEC_COMPONENTS] = { NULL };
   LLVMValueRef texture_unit_offset = NULL;
   LLVMValueRef offsets[3] = { NULL };
   LLVMValueRef explicit_lod = NULL, ms_index = NULL;
   LLVMValueRef coord_undef = LLVMGetUndef(bld->base.vec_type);

   for (unsigned i = 0; i < instr->num_srcs; i++) {
      switch (instr->src[i].src_type) {
      case nir_tex_src_coord: {
         LLVMValueRef coord = get_src(bld, instr->src[i].src);
         coords[0] = coord;
         for (unsigned chan = 1; chan < 5; chan++) {
            coords[chan] = coord_undef;
         }
         break;
      }
      case nir_tex_src_texture_deref:
         texture_deref_instr = nir_src_as_deref(instr->src[i].src);
         break;
      case nir_tex_src_sampler_deref:
         sampler_deref_instr = nir_src_as_deref(instr->src[i].src);
         break;
      case nir_tex_src_comparator:
         coords[4] = get_src(bld, instr->src[i].src);
         coords[4] = cast_type(bld, coords[4], nir_type_float, 32);
         break;
      case nir_tex_src_bias:
         explicit_lod = cast_type(bld, get_src(bld, instr->src[i].src), nir_type_float, 32);
         break;
      case nir_tex_src_lod:
         if (instr->op == nir_texop_txf)
            explicit_lod = cast_type(bld, get_src(bld, instr->src[i].src), nir_type_int, 32);
         else
            explicit_lod = cast_type(bld, get_src(bld, instr->src[i].src), nir_type_float, 32);
         break;
      case nir_tex_src_offset: {
         int offset_cnt = instr->coord_components;
         if (instr->is_array)
            offset_cnt--;
         LLVMValueRef offset_val = get_src(bld, instr->src[i].src);
         if (offset_cnt == 1)
            offsets[0] = cast_type(bld, offset_val, nir_type_int, 32);
         else {
            for (unsigned chan = 0; chan < offset_cnt; ++chan) {
               offsets[chan] = LLVMBuildExtractValue(builder, offset_val,
                                                     chan, "");
               offsets[chan] = cast_type(bld, offsets[chan], nir_type_int, 32);
            }
         }
         break;
      }
      case nir_tex_src_ms_index:
         ms_index = cast_type(bld, get_src(bld, instr->src[i].src), nir_type_int, 32);
         break;

      case nir_tex_src_texture_offset:
         texture_unit_offset = get_src(bld, instr->src[i].src);
         break;
      case nir_tex_src_sampler_offset:
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

   coords[0] = cast_type(bld, coords[0], nir_type_float, 32);

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

   params.sample_key = lp_build_nir_sample_key(bld->shader->info.stage, instr);
   params.offsets = offsets;
   params.texture_index = tex_base_index;
   params.texture_index_offset = texture_unit_offset;
   params.sampler_index = samp_base_index;
   params.coords = coords;
   params.texel = texel;
   params.lod = explicit_lod;
   params.ms_index = ms_index;
   emit_tex(bld, &params);

   assign_ssa_dest(bld, &instr->def, texel);
}

static void
visit_block(struct lp_build_nir_aos_context *bld, nir_block *block)
{
   nir_foreach_instr(instr, block)
   {
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
      case nir_instr_type_deref:
         break;
      default:
         fprintf(stderr, "Unknown NIR instr type: ");
         nir_print_instr(instr, stderr);
         fprintf(stderr, "\n");
         abort();
      }
   }
}

static void
visit_cf_list(struct lp_build_nir_aos_context *bld,
              struct exec_list *list)
{
   foreach_list_typed(nir_cf_node, node, node, list)
   {
      switch (node->type) {
      case nir_cf_node_block:
         visit_block(bld, nir_cf_node_as_block(node));
         break;
      case nir_cf_node_if:
      case nir_cf_node_loop:
      default:
         assert(0);
      }
   }
}

void
lp_build_nir_aos(struct gallivm_state *gallivm,
                 struct nir_shader *shader,
                 struct lp_type type,
                 const unsigned char swizzles[4],
                 LLVMValueRef consts_ptr,
                 const LLVMValueRef *inputs,
                 LLVMValueRef *outputs,
                 const struct lp_build_sampler_aos *sampler)
{
   struct lp_build_nir_aos_context bld;

   memset(&bld, 0, sizeof bld);
   lp_build_context_init(&bld.base, gallivm, type);
   lp_build_context_init(&bld.uint_bld, gallivm, lp_uint_type(type));
   lp_build_context_init(&bld.int_bld, gallivm, lp_int_type(type));

   for (unsigned chan = 0; chan < 4; ++chan) {
      bld.swizzles[chan] = swizzles[chan];
      bld.inv_swizzles[swizzles[chan]] = chan;
   }
   bld.sampler = sampler;

   bld.inputs = inputs;
   bld.outputs = outputs;
   bld.consts_ptr = consts_ptr;

   NIR_PASS(_, shader, nir_convert_to_lcssa, true, true);
   NIR_PASS(_, shader, nir_convert_from_ssa, true, false);
   NIR_PASS(_, shader, nir_lower_locals_to_regs, 32);
   NIR_PASS(_, shader, nir_remove_dead_derefs);
   NIR_PASS(_, shader, nir_remove_dead_variables, nir_var_function_temp, NULL);
   NIR_PASS(_, shader, nir_move_vec_src_uses_to_dest, false);
   NIR_PASS(_, shader, nir_lower_vec_to_regs, NULL, NULL);

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

   bld.regs = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                      _mesa_key_pointer_equal);

   bld.shader = shader;

   nir_function_impl *impl = nir_shader_get_entrypoint(shader);
   nir_foreach_reg_decl(reg, impl) {
      LLVMTypeRef type = bld.base.int_vec_type;
      LLVMValueRef reg_alloc = lp_build_alloca(gallivm, type, "reg");
      _mesa_hash_table_insert(bld.regs, reg, reg_alloc);
   }
   nir_index_ssa_defs(impl);
   bld.ssa_defs = calloc(impl->ssa_alloc, sizeof(LLVMValueRef));
   visit_cf_list(&bld, &impl->body);

   free(bld.ssa_defs);
   ralloc_free(bld.regs);
}
