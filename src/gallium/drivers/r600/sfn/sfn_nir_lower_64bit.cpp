/* -*- mesa-c++  -*-
 * Copyright 2020 Collabora LTD
 * Author: Gert Wollny <gert.wollny@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"
#include "nir_builder.h"
#include "sfn_nir.h"

#include <iostream>
#include <map>
#include <vector>

namespace r600 {

using std::make_pair;
using std::map;
using std::pair;
using std::vector;

class LowerSplit64BitVar : public NirLowerInstruction {
public:
   ~LowerSplit64BitVar();
   using VarSplit = pair<nir_variable *, nir_variable *>;
   using VarMap = map<unsigned, VarSplit>;

   nir_def *split_double_load_deref(nir_intrinsic_instr *intr);

   nir_def *split_double_store_deref(nir_intrinsic_instr *intr);

private:
   nir_def *split_load_deref_array(nir_intrinsic_instr *intr, nir_src& index);

   nir_def *split_load_deref_var(nir_intrinsic_instr *intr);

   nir_def *split_store_deref_array(nir_intrinsic_instr *intr,
                                        nir_deref_instr *deref);

   nir_def *split_store_deref_var(nir_intrinsic_instr *intr, nir_deref_instr *deref1);

   VarSplit get_var_pair(nir_variable *old_var);

   nir_def *
   merge_64bit_loads(nir_def *load1, nir_def *load2, bool out_is_vec3);

   nir_def *split_double_load(nir_intrinsic_instr *load1);

   nir_def *split_store_output(nir_intrinsic_instr *store1);

   nir_def *split_double_load_uniform(nir_intrinsic_instr *intr);

   nir_def *split_double_load_ssbo(nir_intrinsic_instr *intr);

   nir_def *split_double_load_ubo(nir_intrinsic_instr *intr);

   nir_def *
   split_reduction(nir_def *src[2][2], nir_op op1, nir_op op2, nir_op reduction);

   nir_def *
   split_reduction3(nir_alu_instr *alu, nir_op op1, nir_op op2, nir_op reduction);

   nir_def *
   split_reduction4(nir_alu_instr *alu, nir_op op1, nir_op op2, nir_op reduction);

   nir_def *split_bcsel(nir_alu_instr *alu);

   nir_def *split_load_const(nir_load_const_instr *lc);

   bool filter(const nir_instr *instr) const override;
   nir_def *lower(nir_instr *instr) override;

   VarMap m_varmap;
   vector<nir_variable *> m_old_vars;
   vector<nir_instr *> m_old_stores;
};

class LowerLoad64Uniform : public NirLowerInstruction {
   bool filter(const nir_instr *instr) const override;
   nir_def *lower(nir_instr *instr) override;
};

bool
LowerLoad64Uniform::filter(const nir_instr *instr) const
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   auto intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_load_uniform &&
       intr->intrinsic != nir_intrinsic_load_ubo &&
       intr->intrinsic != nir_intrinsic_load_ubo_vec4)
      return false;

   return intr->def.bit_size == 64;
}

nir_def *
LowerLoad64Uniform::lower(nir_instr *instr)
{
   auto intr = nir_instr_as_intrinsic(instr);
   int old_components = intr->def.num_components;
   assert(old_components <= 2);
   intr->def.num_components *= 2;
   intr->def.bit_size = 32;
   intr->num_components *= 2;

   if (intr->intrinsic == nir_intrinsic_load_ubo ||
       intr->intrinsic == nir_intrinsic_load_ubo_vec4)
      nir_intrinsic_set_component(intr, 2 * nir_intrinsic_component(intr));

   nir_def *result_vec[2] = {nullptr, nullptr};

   for (int i = 0; i < old_components; ++i) {
      result_vec[i] = nir_pack_64_2x32_split(b,
                                             nir_channel(b, &intr->def, 2 * i),
                                             nir_channel(b, &intr->def, 2 * i + 1));
   }
   if (old_components == 1)
      return result_vec[0];

   return nir_vec2(b, result_vec[0], result_vec[1]);
}

bool
r600_split_64bit_uniforms_and_ubo(nir_shader *sh)
{
   return LowerLoad64Uniform().run(sh);
}

class LowerSplit64op : public NirLowerInstruction {
   bool filter(const nir_instr *instr) const override
   {
      switch (instr->type) {
      case nir_instr_type_alu: {
         auto alu = nir_instr_as_alu(instr);
         switch (alu->op) {
         case nir_op_bcsel:
            return alu->def.bit_size == 64;
         case nir_op_f2i32:
         case nir_op_f2u32:
         case nir_op_f2i64:
         case nir_op_f2u64:
            return nir_src_bit_size(alu->src[0].src) == 64;
         case nir_op_i2f64:
         case nir_op_u2f64:
            return nir_src_bit_size(alu->src[0].src) >= 32;
         case nir_op_b2f64:
            return true;
         default:
            return false;
         }
      }
      case nir_instr_type_phi: {
         auto phi = nir_instr_as_phi(instr);
         return phi->def.num_components == 64;
      }
      default:
         return false;
      }
   }

   nir_def *lower(nir_instr *instr) override
   {

      switch (instr->type) {
      case nir_instr_type_alu: {
         auto alu = nir_instr_as_alu(instr);
         switch (alu->op) {

         case nir_op_bcsel: {
            auto lo =
               nir_bcsel(b,
                         alu->src[0].src.ssa,
                         nir_unpack_64_2x32_split_x(b, nir_ssa_for_alu_src(b, alu, 1)),
                         nir_unpack_64_2x32_split_x(b, nir_ssa_for_alu_src(b, alu, 2)));
            auto hi =
               nir_bcsel(b,
                         alu->src[0].src.ssa,
                         nir_unpack_64_2x32_split_y(b, nir_ssa_for_alu_src(b, alu, 1)),
                         nir_unpack_64_2x32_split_y(b, nir_ssa_for_alu_src(b, alu, 2)));
            return nir_pack_64_2x32_split(b, lo, hi);
         }
         case nir_op_f2i32: {
            auto src = nir_ssa_for_alu_src(b, alu, 0);
            auto gt0 = nir_fgt_imm(b, src, 0.0);
            auto abs_src = nir_fabs(b, src);
            auto value = nir_f2u32(b, abs_src);
            return nir_bcsel(b, gt0, value, nir_ineg(b, value));
         }
         case nir_op_f2u32: {
            /* fp32 doesn't hold sufficient bits to represent the full range of
             * u32, therefore we have to split the values, and because f2f32
             * rounds, we have to remove the fractional part in the hi bits
             * For values > UINT_MAX the result is undefined */
            auto src = nir_ssa_for_alu_src(b, alu, 0);
            src = nir_fadd(b, src, nir_fneg(b, nir_ffract(b, src)));
            auto gt0 = nir_fgt_imm(b, src, 0.0);
            auto highval = nir_fmul_imm(b, src, 1.0 / 65536.0);
            auto fract = nir_ffract(b, highval);
            auto high = nir_f2u32(b, nir_f2f32(b, nir_fadd(b, highval, nir_fneg(b, fract))));
            auto lowval = nir_fmul_imm(b, fract, 65536.0);
            auto low = nir_f2u32(b, nir_f2f32(b, lowval));
            return nir_bcsel(b,
                             gt0,
                             nir_ior(b, nir_ishl_imm(b, high, 16), low),
                             nir_imm_int(b, 0));
         }        
         case nir_op_u2f64: {
            auto src = nir_ssa_for_alu_src(b, alu, 0);
            if (src->bit_size == 64) {
               return lower_i64_to_f64(src, nir_op_u2f64);
            } else {
               return lower_i32_to_f64(src, nir_op_u2f32);
            }
         }
         case nir_op_i2f64: {
            auto src = nir_ssa_for_alu_src(b, alu, 0);
            if (src->bit_size == 64) {
               return lower_i64_to_f64(src, nir_op_i2f64);
            } else {
               return lower_i32_to_f64(src, nir_op_i2f32);
            }
         }
         case nir_op_b2f64: {
            auto src = nir_b2b32(b, nir_ssa_for_alu_src(b, alu, 0));
            return nir_pack_64_2x32_split(b,
                                          nir_imm_zero(b, 1, 32),
                                          nir_iand(b, src, nir_imm_int(b, 0x3ff00000)));
         }
         default:
            UNREACHABLE("trying to lower instruction that was not in filter");
         }
      }
      case nir_instr_type_phi: {
         auto phi = nir_instr_as_phi(instr);
         auto phi_lo = nir_phi_instr_create(b->shader);
         auto phi_hi = nir_phi_instr_create(b->shader);
         nir_def_init(
            &phi_lo->instr, &phi_lo->def, phi->def.num_components * 2, 32);
         nir_def_init(
            &phi_hi->instr, &phi_hi->def, phi->def.num_components * 2, 32);
         nir_foreach_phi_src(s, phi)
         {
            auto lo = nir_unpack_32_2x16_split_x(b, s->src.ssa);
            auto hi = nir_unpack_32_2x16_split_x(b, s->src.ssa);
            nir_phi_instr_add_src(phi_lo, s->pred, lo);
            nir_phi_instr_add_src(phi_hi, s->pred, hi);
         }
         return nir_pack_64_2x32_split(b, &phi_lo->def, &phi_hi->def);
      }
      default:
         UNREACHABLE("Trying to lower instruction that was not in filter");
      }
   }

   nir_def *lower_i64_to_f64(nir_def *src, nir_op op)
   {
      auto flow = nir_i2f64(b, nir_unpack_64_2x32_split_x(b, src));
      auto fhigh = nir_build_alu1(b, op, nir_unpack_64_2x32_split_y(b, src));
      return nir_fadd(b, nir_fmul_imm(b, fhigh, 65536.0 * 65536.0), flow);
   }

   nir_def *lower_i32_to_f64(nir_def *src, nir_op op)
   {
      auto tmplo = nir_u2f32(b, nir_iand(b, src, nir_imm_int(b, 0x000000ff)));
      auto tmphi =
         nir_build_alu1(b, op, nir_iand(b, src, nir_imm_int(b, 0xffffff00)));
      auto f64 = nir_f2f64(b, nir_vec2(b, tmplo, tmphi));
      return nir_fadd(b, nir_channel(b, f64, 0), nir_channel(b, f64, 1));
   }
};

bool
r600_split_64bit_alu_and_phi(nir_shader *sh)
{
   return LowerSplit64op().run(sh);
}

bool
LowerSplit64BitVar::filter(const nir_instr *instr) const
{
   switch (instr->type) {
   case nir_instr_type_intrinsic: {
      auto intr = nir_instr_as_intrinsic(instr);

      switch (intr->intrinsic) {
      case nir_intrinsic_load_deref:
      case nir_intrinsic_load_uniform:
      case nir_intrinsic_load_input:
      case nir_intrinsic_load_ubo:
      case nir_intrinsic_load_ssbo:
         if (intr->def.bit_size != 64)
            return false;
         return intr->def.num_components >= 3;
      case nir_intrinsic_store_output:
         if (nir_src_bit_size(intr->src[0]) != 64)
            return false;
         return nir_src_num_components(intr->src[0]) >= 3;
      case nir_intrinsic_store_deref:
         if (nir_src_bit_size(intr->src[1]) != 64)
            return false;
         return nir_src_num_components(intr->src[1]) >= 3;
      default:
         return false;
      }
   }
   case nir_instr_type_alu: {
      auto alu = nir_instr_as_alu(instr);
      switch (alu->op) {
      case nir_op_bcsel:
         if (alu->def.num_components < 3)
            return false;
         return alu->def.bit_size == 64;
      case nir_op_bany_fnequal3:
      case nir_op_bany_fnequal4:
      case nir_op_ball_fequal3:
      case nir_op_ball_fequal4:
      case nir_op_bany_inequal3:
      case nir_op_bany_inequal4:
      case nir_op_ball_iequal3:
      case nir_op_ball_iequal4:
      case nir_op_fdot3:
      case nir_op_fdot4:
         return nir_src_bit_size(alu->src[1].src) == 64;
      default:
         return false;
      }
   }
   case nir_instr_type_load_const: {
      auto lc = nir_instr_as_load_const(instr);
      if (lc->def.bit_size != 64)
         return false;
      return lc->def.num_components >= 3;
   }
   default:
      return false;
   }
}

nir_def *
LowerSplit64BitVar::merge_64bit_loads(nir_def *load1,
                                      nir_def *load2,
                                      bool out_is_vec3)
{
   if (out_is_vec3)
      return nir_vec3(b,
                      nir_channel(b, load1, 0),
                      nir_channel(b, load1, 1),
                      nir_channel(b, load2, 0));
   else
      return nir_vec4(b,
                      nir_channel(b, load1, 0),
                      nir_channel(b, load1, 1),
                      nir_channel(b, load2, 0),
                      nir_channel(b, load2, 1));
}

LowerSplit64BitVar::~LowerSplit64BitVar()
{
   for (auto&& v : m_old_vars)
      exec_node_remove(&v->node);

   for (auto&& v : m_old_stores)
      nir_instr_remove(v);
}

nir_def *
LowerSplit64BitVar::split_double_store_deref(nir_intrinsic_instr *intr)
{
   auto deref = nir_def_as_deref(intr->src[0].ssa);
   if (deref->deref_type == nir_deref_type_var)
      return split_store_deref_var(intr, deref);
   else if (deref->deref_type == nir_deref_type_array)
      return split_store_deref_array(intr, deref);
   else {
      UNREACHABLE("only splitting of stores to vars and arrays is supported");
   }
}

nir_def *
LowerSplit64BitVar::split_double_load_deref(nir_intrinsic_instr *intr)
{
   auto deref = nir_def_as_deref(intr->src[0].ssa);
   if (deref->deref_type == nir_deref_type_var)
      return split_load_deref_var(intr);
   else if (deref->deref_type == nir_deref_type_array)
      return split_load_deref_array(intr, deref->arr.index);
   else {
      UNREACHABLE("only splitting of loads from vars and arrays is supported");
   }
   m_old_stores.push_back(&intr->instr);
}

nir_def *
LowerSplit64BitVar::split_load_deref_array(nir_intrinsic_instr *intr, nir_src& index)
{
   auto old_var = nir_intrinsic_get_var(intr, 0);
   unsigned old_components = glsl_get_components(glsl_without_array(old_var->type));

   assert(old_components > 2 && old_components <= 4);

   auto vars = get_var_pair(old_var);

   auto deref1 = nir_build_deref_var(b, vars.first);
   auto deref_array1 = nir_build_deref_array(b, deref1, index.ssa);
   auto load1 =
      nir_build_load_deref(b, 2, 64, &deref_array1->def, (enum gl_access_qualifier)0);

   auto deref2 = nir_build_deref_var(b, vars.second);
   auto deref_array2 = nir_build_deref_array(b, deref2, index.ssa);

   auto load2 = nir_build_load_deref(
      b, old_components - 2, 64, &deref_array2->def, (enum gl_access_qualifier)0);

   return merge_64bit_loads(load1, load2, old_components == 3);
}

nir_def *
LowerSplit64BitVar::split_store_deref_array(nir_intrinsic_instr *intr,
                                            nir_deref_instr *deref)
{
   auto old_var = nir_intrinsic_get_var(intr, 0);
   unsigned old_components = glsl_get_components(glsl_without_array(old_var->type));

   assert(old_components > 2 && old_components <= 4);

   auto src_xy = nir_trim_vector(b, intr->src[1].ssa, 2);

   auto vars = get_var_pair(old_var);

   auto deref1 = nir_build_deref_var(b, vars.first);
   auto deref_array1 =
      nir_build_deref_array(b, deref1, deref->arr.index.ssa);

   nir_build_store_deref(b, &deref_array1->def, src_xy, 3);

   auto deref2 = nir_build_deref_var(b, vars.second);
   auto deref_array2 =
      nir_build_deref_array(b, deref2, deref->arr.index.ssa);

   if (old_components == 3)
      nir_build_store_deref(b,
                            &deref_array2->def,
                            nir_channel(b, intr->src[1].ssa, 2),
                            1);
   else
      nir_build_store_deref(b,
                            &deref_array2->def,
                            nir_channels(b, intr->src[1].ssa, 0xc),
                            3);

   return NIR_LOWER_INSTR_PROGRESS_REPLACE;
}

nir_def *
LowerSplit64BitVar::split_store_deref_var(nir_intrinsic_instr *intr,
                                          UNUSED nir_deref_instr *deref)
{
   auto old_var = nir_intrinsic_get_var(intr, 0);
   unsigned old_components = glsl_get_components(glsl_without_array(old_var->type));

   assert(old_components > 2 && old_components <= 4);

   auto src_xy = nir_trim_vector(b, intr->src[1].ssa, 2);

   auto vars = get_var_pair(old_var);

   auto deref1 = nir_build_deref_var(b, vars.first);
   nir_build_store_deref(b, &deref1->def, src_xy, 3);

   auto deref2 = nir_build_deref_var(b, vars.second);
   if (old_components == 3)
      nir_build_store_deref(b, &deref2->def, nir_channel(b, intr->src[1].ssa, 2), 1);
   else
      nir_build_store_deref(b,
                            &deref2->def,
                            nir_channels(b, intr->src[1].ssa, 0xc),
                            3);

   return NIR_LOWER_INSTR_PROGRESS_REPLACE;
}

nir_def *
LowerSplit64BitVar::split_load_deref_var(nir_intrinsic_instr *intr)
{
   auto old_var = nir_intrinsic_get_var(intr, 0);
   auto vars = get_var_pair(old_var);
   unsigned old_components = glsl_get_components(old_var->type);

   nir_deref_instr *deref1 = nir_build_deref_var(b, vars.first);
   auto *load1 = nir_load_deref(b, deref1);

   nir_deref_instr *deref2 = nir_build_deref_var(b, vars.second);
   deref2->type = vars.second->type;

   auto *load2 = nir_load_deref(b, deref2);

   return merge_64bit_loads(load1, load2, old_components == 3);
}

LowerSplit64BitVar::VarSplit
LowerSplit64BitVar::get_var_pair(nir_variable *old_var)
{
   auto split_vars = m_varmap.find(old_var->data.driver_location);

   assert(glsl_get_components(glsl_without_array(old_var->type)) > 2);

   if (split_vars == m_varmap.end()) {
      auto var1 = nir_variable_clone(old_var, b->shader);
      auto var2 = nir_variable_clone(old_var, b->shader);

      var1->type = glsl_dvec_type(2);
      var2->type = glsl_dvec_type(glsl_get_components(glsl_without_array(old_var->type)) - 2);

      if (glsl_type_is_array(old_var->type)) {
         var1->type = glsl_array_type(var1->type, glsl_array_size(old_var->type), 0);
         var2->type = glsl_array_type(var2->type, glsl_array_size(old_var->type), 0);
      }

      if (old_var->data.mode == nir_var_shader_in ||
          old_var->data.mode == nir_var_shader_out) {
         ++var2->data.driver_location;
         ++var2->data.location;
         nir_shader_add_variable(b->shader, var1);
         nir_shader_add_variable(b->shader, var2);
      } else if (old_var->data.mode == nir_var_function_temp) {
         exec_list_push_tail(&b->impl->locals, &var1->node);
         exec_list_push_tail(&b->impl->locals, &var2->node);
      }

      m_varmap[old_var->data.driver_location] = make_pair(var1, var2);
   }
   return m_varmap[old_var->data.driver_location];
}

nir_def *
LowerSplit64BitVar::split_double_load(nir_intrinsic_instr *load1)
{
   unsigned old_components = load1->def.num_components;
   auto load2 = nir_instr_as_intrinsic(nir_instr_clone(b->shader, &load1->instr));
   nir_io_semantics sem = nir_intrinsic_io_semantics(load1);

   load1->def.num_components = 2;
   sem.num_slots = 1;
   nir_intrinsic_set_io_semantics(load1, sem);

   load2->def.num_components = old_components - 2;
   sem.location += 1;
   nir_intrinsic_set_io_semantics(load2, sem);
   nir_intrinsic_set_base(load2, nir_intrinsic_base(load1) + 1);
   nir_builder_instr_insert(b, &load2->instr);

   return merge_64bit_loads(&load1->def, &load2->def, old_components == 3);
}

nir_def *
LowerSplit64BitVar::split_store_output(nir_intrinsic_instr *store1)
{
   auto src = store1->src[0];
   unsigned old_components = nir_src_num_components(src);
   nir_io_semantics sem = nir_intrinsic_io_semantics(store1);

   auto store2 = nir_instr_as_intrinsic(nir_instr_clone(b->shader, &store1->instr));
   auto src1 = nir_trim_vector(b, src.ssa, 2);
   auto src2 = nir_channels(b, src.ssa, old_components == 3 ? 4 : 0xc);

   nir_src_rewrite(&src, src1);
   nir_intrinsic_set_write_mask(store1, 3);

   nir_src_rewrite(&src, src2);
   nir_intrinsic_set_write_mask(store2, old_components == 3 ? 1 : 3);

   sem.num_slots = 1;
   nir_intrinsic_set_io_semantics(store1, sem);

   sem.location += 1;
   nir_intrinsic_set_io_semantics(store2, sem);
   nir_intrinsic_set_base(store2, nir_intrinsic_base(store1));

   nir_builder_instr_insert(b, &store2->instr);
   return NIR_LOWER_INSTR_PROGRESS;
}

nir_def *
LowerSplit64BitVar::split_double_load_uniform(nir_intrinsic_instr *intr)
{
   unsigned second_components = intr->def.num_components - 2;
   nir_intrinsic_instr *load2 =
      nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_uniform);
   load2->src[0] = nir_src_for_ssa(nir_iadd_imm(b, intr->src[0].ssa, 1));
   nir_intrinsic_set_dest_type(load2, nir_intrinsic_dest_type(intr));
   nir_intrinsic_set_base(load2, nir_intrinsic_base(intr));
   nir_intrinsic_set_range(load2, nir_intrinsic_range(intr));
   load2->num_components = second_components;

   nir_def_init(&load2->instr, &load2->def, second_components, 64);
   nir_builder_instr_insert(b, &load2->instr);

   intr->def.num_components = intr->num_components = 2;

   if (second_components == 1)
      return nir_vec3(b,
                      nir_channel(b, &intr->def, 0),
                      nir_channel(b, &intr->def, 1),
                      nir_channel(b, &load2->def, 0));
   else
      return nir_vec4(b,
                      nir_channel(b, &intr->def, 0),
                      nir_channel(b, &intr->def, 1),
                      nir_channel(b, &load2->def, 0),
                      nir_channel(b, &load2->def, 1));
}

nir_def *
LowerSplit64BitVar::split_double_load_ssbo(nir_intrinsic_instr *intr)
{
   unsigned second_components = intr->def.num_components - 2;
   nir_intrinsic_instr *load2 =
      nir_instr_as_intrinsic(nir_instr_clone(b->shader, &intr->instr));

   nir_src_rewrite(&load2->src[0], nir_iadd_imm(b, intr->src[0].ssa, 1));
   load2->num_components = second_components;
   nir_def_init(&load2->instr, &load2->def, second_components, 64);

   nir_intrinsic_set_dest_type(load2, nir_intrinsic_dest_type(intr));
   nir_builder_instr_insert(b, &load2->instr);

   intr->def.num_components = intr->num_components = 2;

   return merge_64bit_loads(&intr->def, &load2->def, second_components == 1);
}

nir_def *
LowerSplit64BitVar::split_double_load_ubo(nir_intrinsic_instr *intr)
{
   unsigned second_components = intr->def.num_components - 2;
   nir_intrinsic_instr *load2 =
      nir_instr_as_intrinsic(nir_instr_clone(b->shader, &intr->instr));
   load2->src[0] = intr->src[0];
   load2->src[1] = nir_src_for_ssa(nir_iadd_imm(b, intr->src[1].ssa, 16));
   nir_intrinsic_set_range_base(load2, nir_intrinsic_range_base(intr) + 16);
   nir_intrinsic_set_range(load2, nir_intrinsic_range(intr));
   nir_intrinsic_set_access(load2, nir_intrinsic_access(intr));
   nir_intrinsic_set_align_mul(load2, nir_intrinsic_align_mul(intr));
   nir_intrinsic_set_align_offset(load2, nir_intrinsic_align_offset(intr));

   load2->num_components = second_components;

   nir_def_init(&load2->instr, &load2->def, second_components, 64);
   nir_builder_instr_insert(b, &load2->instr);

   intr->def.num_components = intr->num_components = 2;

   return merge_64bit_loads(&intr->def, &load2->def, second_components == 1);
}

nir_def *
LowerSplit64BitVar::split_reduction(nir_def *src[2][2],
                                    nir_op op1,
                                    nir_op op2,
                                    nir_op reduction)
{
   auto cmp0 = nir_build_alu(b, op1, src[0][0], src[0][1], nullptr, nullptr);
   auto cmp1 = nir_build_alu(b, op2, src[1][0], src[1][1], nullptr, nullptr);
   return nir_build_alu(b, reduction, cmp0, cmp1, nullptr, nullptr);
}

nir_def *
LowerSplit64BitVar::split_reduction3(nir_alu_instr *alu,
                                     nir_op op1,
                                     nir_op op2,
                                     nir_op reduction)
{
   nir_def *src[2][2];

   src[0][0] = nir_trim_vector(b, alu->src[0].src.ssa, 2);
   src[0][1] = nir_trim_vector(b, alu->src[1].src.ssa, 2);

   src[1][0] = nir_channel(b, alu->src[0].src.ssa, 2);
   src[1][1] = nir_channel(b, alu->src[1].src.ssa, 2);

   return split_reduction(src, op1, op2, reduction);
}

nir_def *
LowerSplit64BitVar::split_reduction4(nir_alu_instr *alu,
                                     nir_op op1,
                                     nir_op op2,
                                     nir_op reduction)
{
   nir_def *src[2][2];

   src[0][0] = nir_trim_vector(b, alu->src[0].src.ssa, 2);
   src[0][1] = nir_trim_vector(b, alu->src[1].src.ssa, 2);

   src[1][0] = nir_channels(b, alu->src[0].src.ssa, 0xc);
   src[1][1] = nir_channels(b, alu->src[1].src.ssa, 0xc);

   return split_reduction(src, op1, op2, reduction);
}

nir_def *
LowerSplit64BitVar::split_bcsel(nir_alu_instr *alu)
{
   static nir_def *dest[4];
   for (unsigned i = 0; i < alu->def.num_components; ++i) {
      dest[i] = nir_bcsel(b,
                          nir_channel(b, alu->src[0].src.ssa, i),
                          nir_channel(b, alu->src[1].src.ssa, i),
                          nir_channel(b, alu->src[2].src.ssa, i));
   }
   return nir_vec(b, dest, alu->def.num_components);
}

nir_def *
LowerSplit64BitVar::split_load_const(nir_load_const_instr *lc)
{
   nir_def *ir[4];
   for (unsigned i = 0; i < lc->def.num_components; ++i)
      ir[i] = nir_imm_double(b, lc->value[i].f64);

   return nir_vec(b, ir, lc->def.num_components);
}

nir_def *
LowerSplit64BitVar::lower(nir_instr *instr)
{
   switch (instr->type) {
   case nir_instr_type_intrinsic: {
      auto intr = nir_instr_as_intrinsic(instr);
      switch (intr->intrinsic) {
      case nir_intrinsic_load_deref:
         return this->split_double_load_deref(intr);
      case nir_intrinsic_load_uniform:
         return split_double_load_uniform(intr);
      case nir_intrinsic_load_ubo:
         return split_double_load_ubo(intr);
      case nir_intrinsic_load_ssbo:
         return split_double_load_ssbo(intr);
      case nir_intrinsic_load_input:
         return split_double_load(intr);
      case nir_intrinsic_store_output:
         return split_store_output(intr);
      case nir_intrinsic_store_deref:
         return split_double_store_deref(intr);
      default:
         assert(0);
      }
   }
   case nir_instr_type_alu: {
      auto alu = nir_instr_as_alu(instr);
      switch (alu->op) {
      case nir_op_bany_fnequal3:
         return split_reduction3(alu, nir_op_bany_fnequal2, nir_op_fneu, nir_op_ior);
      case nir_op_ball_fequal3:
         return split_reduction3(alu, nir_op_ball_fequal2, nir_op_feq, nir_op_iand);
      case nir_op_bany_inequal3:
         return split_reduction3(alu, nir_op_bany_inequal2, nir_op_ine, nir_op_ior);
      case nir_op_ball_iequal3:
         return split_reduction3(alu, nir_op_ball_iequal2, nir_op_ieq, nir_op_iand);
      case nir_op_fdot3:
         return split_reduction3(alu, nir_op_fdot2, nir_op_fmul, nir_op_fadd);
      case nir_op_bany_fnequal4:
         return split_reduction4(alu,
                                 nir_op_bany_fnequal2,
                                 nir_op_bany_fnequal2,
                                 nir_op_ior);
      case nir_op_ball_fequal4:
         return split_reduction4(alu,
                                 nir_op_ball_fequal2,
                                 nir_op_ball_fequal2,
                                 nir_op_iand);
      case nir_op_bany_inequal4:
         return split_reduction4(alu,
                                 nir_op_bany_inequal2,
                                 nir_op_bany_inequal2,
                                 nir_op_ior);
      case nir_op_ball_iequal4:
         return split_reduction4(alu,
                                 nir_op_bany_fnequal2,
                                 nir_op_bany_fnequal2,
                                 nir_op_ior);
      case nir_op_fdot4:
         return split_reduction4(alu, nir_op_fdot2, nir_op_fdot2, nir_op_fadd);
      case nir_op_bcsel:
         return split_bcsel(alu);
      default:
         assert(0);
      }
   }
   case nir_instr_type_load_const: {
      auto lc = nir_instr_as_load_const(instr);
      return split_load_const(lc);
   }
   default:
      assert(0);
   }
   return nullptr;
}

/* Split 64 bit instruction so that at most two 64 bit components are
 * used in one instruction */

bool
r600_nir_split_64bit_io(nir_shader *sh)
{
   return LowerSplit64BitVar().run(sh);
}

/* */
class Lower64BitToVec2 : public NirLowerInstruction {
public:
   Lower64BitToVec2(bool support_fp64):
       m_support_fp64(support_fp64)
   {
   }

private:
   bool filter(const nir_instr *instr) const override;
   nir_def *lower(nir_instr *instr) override;

   nir_def *load_uniform_64_to_vec2(nir_intrinsic_instr *intr);
   nir_def *load_ssbo_64_to_vec2(nir_intrinsic_instr *intr);
   nir_def *load_64_to_vec2(nir_intrinsic_instr *intr);

   nir_def *store_ssbo_64_to_vec2(nir_intrinsic_instr *intr);

   nir_def *make_64bit(nir_def *def, int ncomp64);

   bool support_fp64_op(nir_op op) const;

   bool m_support_fp64{false};
};

bool
Lower64BitToVec2::filter(const nir_instr *instr) const
{
   switch (instr->type) {
   case nir_instr_type_intrinsic: {
      auto intr = nir_instr_as_intrinsic(instr);

      switch (intr->intrinsic) {
      case nir_intrinsic_load_deref:
      case nir_intrinsic_load_input:
      case nir_intrinsic_load_uniform:
      case nir_intrinsic_load_ubo:
      case nir_intrinsic_load_global:
      case nir_intrinsic_load_global_constant:
      case nir_intrinsic_load_ubo_vec4:
      case nir_intrinsic_load_ssbo:
      case nir_intrinsic_load_reg:
      case nir_intrinsic_load_reg_indirect:
         return intr->def.bit_size == 64;
      case nir_intrinsic_store_global:
      case nir_intrinsic_store_ssbo:
         return nir_src_bit_size(intr->src[0]) == 64;
      default:
         return false;
      }
   }
   case nir_instr_type_alu: {
      auto alu = nir_instr_as_alu(instr);
      return (!support_fp64_op(alu->op) && alu->def.bit_size == 64 &&
              alu->src[0].src.ssa->bit_size == 64);
   }
   case nir_instr_type_load_const: {
      auto lc = nir_instr_as_load_const(instr);
      return lc->def.bit_size == 64;
   }
   case nir_instr_type_undef: {
      auto undef = nir_instr_as_undef(instr);
      return undef->def.bit_size == 64;
   }
   default:
      return false;
   }
}

nir_def *
Lower64BitToVec2::lower(nir_instr *instr)
{
   switch (instr->type) {
   case nir_instr_type_intrinsic: {
      auto intr = nir_instr_as_intrinsic(instr);
      switch (intr->intrinsic) {
      case nir_intrinsic_load_uniform:
         return load_uniform_64_to_vec2(intr);
      case nir_intrinsic_load_ssbo:
         return load_ssbo_64_to_vec2(intr);
      case nir_intrinsic_load_input:
      case nir_intrinsic_load_global:
      case nir_intrinsic_load_global_constant:
      case nir_intrinsic_load_ubo:
      case nir_intrinsic_load_ubo_vec4:
         return load_64_to_vec2(intr);
      case nir_intrinsic_store_ssbo:
         return store_ssbo_64_to_vec2(intr);
      default:

         return nullptr;
      }
   }
   case nir_instr_type_alu: {
      auto alu = nir_instr_as_alu(instr);

      nir_def *s[4] = {nullptr};
      auto alu_info = nir_op_infos[alu->op];
      for (unsigned i = 0; i < alu_info.num_inputs; ++i) {
         const auto& src = alu->src[i].src.ssa;
         const auto& swz = alu->src[i].swizzle;
         if (alu->def.num_components == 1)
            s[i] = nir_vec2(b,
                            nir_unpack_64_2x32_split_x(b, nir_channel(b, src, swz[0])),
                            nir_unpack_64_2x32_split_y(b, nir_channel(b, src, swz[0])));
         else
            s[i] = nir_vec4(b,
                            nir_unpack_64_2x32_split_x(b, nir_channel(b, src, swz[0])),
                            nir_unpack_64_2x32_split_y(b, nir_channel(b, src, swz[0])),
                            nir_unpack_64_2x32_split_x(b, nir_channel(b, src, swz[1])),
                            nir_unpack_64_2x32_split_y(b, nir_channel(b, src, swz[1])));
      }
      auto d = nir_build_alu(b, alu->op, s[0], s[1], s[2], s[3]);
      if (alu->def.num_components == 1)
         return nir_pack_64_2x32(b, d);
      else
         return nir_vec2(b,
                         nir_pack_64_2x32(b, nir_channels(b, d, 3)),
                         nir_pack_64_2x32(b, nir_channels(b, d, 0xC)));
   } break;
   case nir_instr_type_load_const: {
      auto lc = nir_instr_as_load_const(instr);
      assert(lc->def.num_components <= 2);
      nir_const_value val[4];
      for (uint i = 0; i < lc->def.num_components; ++i) {
         uint64_t v = lc->value[i].u64;
         val[i * 2 + 0] = nir_const_value_for_uint(v & 0xffffffff, 32);
         val[i * 2 + 1] = nir_const_value_for_uint(v >> 32, 32);
      }

      return make_64bit(nir_build_imm(b, 2 * lc->def.num_components, 32, val),
                        lc->def.num_components);
      ;
   }
   case nir_instr_type_undef: {
      auto undef = nir_instr_as_undef(instr);
      undef->def.num_components *= 2;
      undef->def.bit_size = 32;
      return NIR_LOWER_INSTR_PROGRESS;
   }
   default:
      return nullptr;
   }
}

nir_def *
Lower64BitToVec2::store_ssbo_64_to_vec2(nir_intrinsic_instr *intr)
{
   b->cursor = nir_before_instr(&intr->instr);

   auto src = intr->src[0].ssa;

   nir_def *s[4];
   for (int i = 0; i < src->num_components; ++i) {
      s[2 * i] = nir_unpack_64_2x32_split_x(b, nir_channel(b, src, i));
      s[2 * i + 1] = nir_unpack_64_2x32_split_y(b, nir_channel(b, src, i));
   }

   auto new_src = src->num_components == 1 ? nir_vec2(b, s[0], s[1])
                                           : nir_vec4(b, s[0], s[1], s[2], s[3]);

   //   intr->num_components *= 2;
   nir_src_rewrite(&intr->src[0], new_src);

   intr->num_components *= 2;
   nir_intrinsic_set_write_mask(intr, src->num_components == 2 ? 0xf : 0x3);

   return NIR_LOWER_INSTR_PROGRESS;
}

nir_def *
Lower64BitToVec2::load_uniform_64_to_vec2(nir_intrinsic_instr *intr)
{
   intr->num_components *= 2;
   intr->def.bit_size = 32;
   intr->def.num_components *= 2;
   nir_intrinsic_set_dest_type(intr, nir_type_float32);
   return make_64bit(&intr->def, intr->num_components >> 1);
}

nir_def *
Lower64BitToVec2::load_64_to_vec2(nir_intrinsic_instr *intr)
{
   intr->num_components *= 2;
   intr->def.bit_size = 32;
   intr->def.num_components *= 2;
   if (nir_intrinsic_has_component(intr))
      nir_intrinsic_set_component(intr, nir_intrinsic_component(intr) * 2);
   return make_64bit(&intr->def, intr->num_components >> 1);
}

nir_def *
Lower64BitToVec2::make_64bit(nir_def *def, int ncomp64)
{
   assert(ncomp64 < 3);

   nir_def *comp[2];

   for (int i = 0; i < ncomp64; ++i) {
      comp[i] = nir_pack_64_2x32(b, nir_channels(b, def, 3 << 2 * i));
   }

   return ncomp64 == 2 ? nir_vec2(b, comp[0], comp[1]) : comp[0];
}

nir_def *
Lower64BitToVec2::load_ssbo_64_to_vec2(nir_intrinsic_instr *intr)
{
   intr->num_components *= 2;
   intr->def.bit_size = 32;
   intr->def.num_components *= 2;
   return make_64bit(&intr->def, intr->num_components >> 1);
}

bool
Lower64BitToVec2::support_fp64_op(nir_op op) const
{
   switch (op) {
   case nir_op_fneg:
   case nir_op_fsat:
   case nir_op_ffract:
   case nir_op_feq32:
   case nir_op_fge32:
   case nir_op_flt32:
   case nir_op_fneu32:
   case nir_op_ffma:
   case nir_op_fadd:
   case nir_op_fmul:
   case nir_op_fmax:
   case nir_op_fmin:
   case nir_op_f2f64:
   case nir_op_f2f32:
   case nir_op_fabs:
   case nir_op_fsqrt:
   case nir_op_frcp:
   case nir_op_frsq:
      return m_support_fp64;
   case nir_op_vec2:
   case nir_op_mov:
      return true;
   default:
      return false;
      ;
   }
}

bool
r600_nir_64_to_vec2(nir_shader *sh, bool support_fp64)
{
   return Lower64BitToVec2(support_fp64).run(sh);
}

using std::map;
using std::pair;
using std::vector;

class StoreMerger {
public:
   StoreMerger(nir_shader *shader);
   void collect_stores();
   bool combine();
   void combine_one_slot(vector<nir_intrinsic_instr *>& stores);

   using StoreCombos = map<unsigned, vector<nir_intrinsic_instr *>>;

   StoreCombos m_stores;
   nir_shader *sh;
};

StoreMerger::StoreMerger(nir_shader *shader):
    sh(shader)
{
}

void
StoreMerger::collect_stores()
{
   unsigned vertex = 0;
   nir_foreach_function_impl(impl, sh)
   {
      nir_foreach_block(block, impl)
      {
         nir_foreach_instr_safe(instr, block)
         {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            auto ir = nir_instr_as_intrinsic(instr);
            if (ir->intrinsic == nir_intrinsic_emit_vertex ||
                ir->intrinsic == nir_intrinsic_emit_vertex_with_counter) {
               ++vertex;
               continue;
            }
            if (ir->intrinsic != nir_intrinsic_store_output)
               continue;

            unsigned index = nir_intrinsic_base(ir) + 64 * vertex +
                             8 * 64 * nir_intrinsic_io_semantics(ir).gs_streams;
            m_stores[index].push_back(ir);
         }
      }
   }
}

bool
StoreMerger::combine()
{
   bool progress = false;
   for (auto&& i : m_stores) {
      if (i.second.size() < 2)
         continue;

      combine_one_slot(i.second);
      progress = true;
   }
   return progress;
}

void
StoreMerger::combine_one_slot(vector<nir_intrinsic_instr *>& stores)
{
   nir_def *srcs[4] = {nullptr};

   auto last_store = *stores.rbegin();

   nir_builder b = nir_builder_at(nir_before_instr(&last_store->instr));

   unsigned comps = 0;
   unsigned writemask = 0;
   unsigned first_comp = 4;
   for (auto&& store : stores) {
      int cmp = nir_intrinsic_component(store);
      for (unsigned i = 0; i < nir_src_num_components(store->src[0]); ++i, ++comps) {
         unsigned out_comp = i + cmp;
         srcs[out_comp] = nir_channel(&b, store->src[0].ssa, i);
         writemask |= 1 << out_comp;
         if (first_comp > out_comp)
            first_comp = out_comp;
      }
   }

   auto new_src = nir_vec(&b, srcs, comps);

   nir_src_rewrite(&last_store->src[0], new_src);
   last_store->num_components = comps;
   nir_intrinsic_set_component(last_store, first_comp);
   nir_intrinsic_set_write_mask(last_store, writemask);

   for (auto i = stores.begin(); i != stores.end() - 1; ++i)
      nir_instr_remove(&(*i)->instr);
}

bool
r600_merge_vec2_stores(nir_shader *shader)
{
   r600::StoreMerger merger(shader);
   merger.collect_stores();
   return merger.combine();
}

} // end namespace r600
