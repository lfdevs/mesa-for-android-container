/*
 * Copyright © 2018 Valve Corporation
 * Copyright © 2018 Google
 *
 * SPDX-License-Identifier: MIT
 */

#include "aco_builder.h"
#include "aco_instruction_selection.h"
#include "aco_ir.h"
#include "aco_nir_call_attribs.h"

#include "util/memstream.h"

#include <optional>

namespace aco {

void
_isel_err(isel_context* ctx, const char* file, unsigned line, const nir_instr* instr,
          const char* msg)
{
   char* out;
   size_t outsize;
   struct u_memstream mem;
   u_memstream_open(&mem, &out, &outsize);
   FILE* const memf = u_memstream_get(&mem);

   fprintf(memf, "%s: ", msg);
   nir_print_instr(instr, memf);
   u_memstream_close(&mem);

   _aco_err(ctx->program, file, line, out);
   free(out);
}

void
append_logical_start(Block* b)
{
   Builder(NULL, b).pseudo(aco_opcode::p_logical_start);
}

void
append_logical_end(isel_context* ctx, bool append_reload_preserved)
{
   Builder bld(ctx->program, ctx->block);

   if (append_reload_preserved && ctx->program->is_callee) {
      Operand stack_ptr_op;
      if (ctx->program->gfx_level >= GFX9)
         stack_ptr_op = Operand(ctx->callee_info.stack_ptr.def.getTemp());
      else
         stack_ptr_op = Operand(load_scratch_resource(ctx->program, bld, -1u, false));
      bld.pseudo(aco_opcode::p_reload_preserved, bld.def(bld.lm), bld.def(s1, scc), stack_ptr_op);
   }

   bld.pseudo(aco_opcode::p_logical_end);
}

Temp
get_ssa_temp_tex(struct isel_context* ctx, nir_def* def, bool is_16bit)
{
   RegClass rc = RegClass::get(RegType::vgpr, (is_16bit ? 2 : 4) * def->num_components);
   Temp tmp = get_ssa_temp(ctx, def);
   if (tmp.bytes() != rc.bytes())
      return emit_extract_vector(ctx, tmp, 0, rc);
   else
      return tmp;
}

Temp
bool_to_vector_condition(isel_context* ctx, Temp val, Temp dst)
{
   Builder bld(ctx->program, ctx->block);
   if (!dst.id())
      dst = bld.tmp(bld.lm);

   assert(val.regClass() == s1);
   assert(dst.regClass() == bld.lm);

   return bld.sop2(Builder::s_cselect, Definition(dst), Operand::c32(-1), Operand::zero(),
                   bld.scc(val));
}

Temp
bool_to_scalar_condition(isel_context* ctx, Temp val, Temp dst)
{
   Builder bld(ctx->program, ctx->block);
   if (!dst.id())
      dst = bld.tmp(s1);

   assert(val.regClass() == bld.lm);
   assert(dst.regClass() == s1);

   /* if we're currently in WQM mode, ensure that the source is also computed in WQM */
   bld.sop2(Builder::s_and, bld.def(bld.lm), bld.scc(Definition(dst)), val, Operand(exec, bld.lm));
   return dst;
}

static Temp
as_vgpr(Builder& bld, Temp val)
{
   if (val.type() == RegType::sgpr)
      return bld.copy(bld.def(RegType::vgpr, val.size()), val);
   assert(val.type() == RegType::vgpr);
   return val;
}

Temp
as_vgpr(isel_context* ctx, Temp val)
{
   Builder bld(ctx->program, ctx->block);
   return as_vgpr(bld, val);
}

Temp
emit_extract_vector(isel_context* ctx, Temp src, uint32_t idx, RegClass dst_rc)
{
   /* no need to extract the whole vector */
   if (src.regClass() == dst_rc) {
      assert(idx == 0);
      return src;
   }

   assert(src.bytes() > (idx * dst_rc.bytes()));
   Builder bld(ctx->program, ctx->block);
   auto it = ctx->allocated_vec.find(src.id());
   if (it != ctx->allocated_vec.end() && dst_rc.bytes() == it->second[idx].regClass().bytes()) {
      if (it->second[idx].regClass() == dst_rc) {
         return it->second[idx];
      } else {
         assert(!dst_rc.is_subdword());
         assert(dst_rc.type() == RegType::vgpr && it->second[idx].type() == RegType::sgpr);
         return bld.copy(bld.def(dst_rc), it->second[idx]);
      }
   }

   if (dst_rc.is_subdword())
      src = as_vgpr(ctx, src);

   if (src.bytes() == dst_rc.bytes()) {
      assert(idx == 0);
      return bld.copy(bld.def(dst_rc), src);
   } else {
      Temp dst = bld.tmp(dst_rc);
      bld.pseudo(aco_opcode::p_extract_vector, Definition(dst), src, Operand::c32(idx));
      return dst;
   }
}

void
emit_split_vector(isel_context* ctx, Temp vec_src, unsigned num_components)
{
   if (num_components == 1)
      return;
   if (ctx->allocated_vec.find(vec_src.id()) != ctx->allocated_vec.end())
      return;
   if (num_components > vec_src.size() && vec_src.type() == RegType::sgpr) {
      /* sub-dword split: should still help get_alu_src() */
      emit_split_vector(ctx, vec_src, vec_src.size());
      return;
   }
   RegClass rc = RegClass::get(vec_src.type(), vec_src.bytes() / num_components);
   aco_ptr<Instruction> split{
      create_instruction(aco_opcode::p_split_vector, Format::PSEUDO, 1, num_components)};
   split->operands[0] = Operand(vec_src);
   std::array<Temp, NIR_MAX_VEC_COMPONENTS> elems;
   for (unsigned i = 0; i < num_components; i++) {
      elems[i] = ctx->program->allocateTmp(rc);
      split->definitions[i] = Definition(elems[i]);
   }
   ctx->block->instructions.emplace_back(std::move(split));
   ctx->allocated_vec.emplace(vec_src.id(), elems);
}

/* This vector expansion uses a mask to determine which elements in the new vector
 * come from the original vector. The other elements are undefined. */
void
expand_vector(isel_context* ctx, Temp vec_src, Temp dst, unsigned num_components, unsigned mask,
              bool zero_padding)
{
   assert(vec_src.type() == RegType::vgpr);
   Builder bld(ctx->program, ctx->block);

   if (dst.type() == RegType::sgpr && num_components > dst.size()) {
      Temp tmp_dst = bld.tmp(RegClass::get(RegType::vgpr, 2 * num_components));
      expand_vector(ctx, vec_src, tmp_dst, num_components, mask, zero_padding);
      bld.pseudo(aco_opcode::p_as_uniform, Definition(dst), tmp_dst);
      ctx->allocated_vec[dst.id()] = ctx->allocated_vec[tmp_dst.id()];
      return;
   }

   emit_split_vector(ctx, vec_src, util_bitcount(mask));

   if (vec_src == dst)
      return;

   if (num_components == 1) {
      if (dst.type() == RegType::sgpr)
         bld.pseudo(aco_opcode::p_as_uniform, Definition(dst), vec_src);
      else
         bld.copy(Definition(dst), vec_src);
      return;
   }

   unsigned component_bytes = dst.bytes() / num_components;
   RegClass src_rc = RegClass::get(RegType::vgpr, component_bytes);
   RegClass dst_rc = RegClass::get(dst.type(), component_bytes);
   assert(dst.type() == RegType::vgpr || !src_rc.is_subdword());
   std::array<Temp, NIR_MAX_VEC_COMPONENTS> elems;

   Temp padding = Temp(0, dst_rc);
   if (zero_padding)
      padding = bld.copy(bld.def(dst_rc), Operand::zero(component_bytes));

   aco_ptr<Instruction> vec{
      create_instruction(aco_opcode::p_create_vector, Format::PSEUDO, num_components, 1)};
   vec->definitions[0] = Definition(dst);
   unsigned k = 0;
   for (unsigned i = 0; i < num_components; i++) {
      if (mask & (1 << i)) {
         Temp src = emit_extract_vector(ctx, vec_src, k++, src_rc);
         if (dst.type() == RegType::sgpr)
            src = bld.as_uniform(src);
         vec->operands[i] = Operand(src);
         elems[i] = src;
      } else {
         vec->operands[i] = Operand::zero(component_bytes);
         elems[i] = padding;
      }
   }
   ctx->block->instructions.emplace_back(std::move(vec));
   ctx->allocated_vec.emplace(dst.id(), elems);
}

/**
 * Copies the first src_bits of the input to the output Temp. Input bits at positions larger than
 * src_bits and dst_bits are truncated.
 *
 * Sign extension may be applied using the sign_extend parameter. The position of the input sign
 * bit is indicated by src_bits in this case.
 *
 * If dst.bytes() is larger than dst_bits/8, the value of the upper bits is undefined.
 */
Temp
convert_int(isel_context* ctx, Builder& bld, Temp src, unsigned src_bits, unsigned dst_bits,
            bool sign_extend, Temp dst)
{
   assert(!(sign_extend && dst_bits < src_bits) &&
          "Shrinking integers is not supported for signed inputs");

   if (!dst.id())
      dst = bld.tmp(RegClass::get(src.type(), dst_bits / 8u));

   assert(src.type() == RegType::sgpr || src_bits == src.bytes() * 8);
   assert(dst.type() == RegType::sgpr || dst_bits == dst.bytes() * 8);

   if (dst.bytes() == src.bytes() && dst_bits < src_bits) {
      /* Copy the raw value, leaving an undefined value in the upper bits for
       * the caller to handle appropriately */
      return bld.copy(Definition(dst), src);
   } else if (dst.bytes() < src.bytes()) {
      return bld.pseudo(aco_opcode::p_extract_vector, Definition(dst), src, Operand::zero());
   }

   Temp tmp = dst;
   if (dst_bits == 64)
      tmp = src_bits == 32 ? src : bld.tmp(src.type(), 1);

   if (tmp == src) {
   } else if (src.regClass() == s1) {
      assert(src_bits < 32);
      bld.pseudo(aco_opcode::p_extract, Definition(tmp), bld.def(s1, scc), src, Operand::zero(),
                 Operand::c32(src_bits), Operand::c32((unsigned)sign_extend));
   } else {
      assert(src_bits < 32);
      bld.pseudo(aco_opcode::p_extract, Definition(tmp), src, Operand::zero(),
                 Operand::c32(src_bits), Operand::c32((unsigned)sign_extend));
   }

   if (dst_bits == 64) {
      if (sign_extend && dst.regClass() == s2) {
         Temp high =
            bld.sop2(aco_opcode::s_ashr_i32, bld.def(s1), bld.def(s1, scc), tmp, Operand::c32(31u));
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), tmp, high);
      } else if (sign_extend && dst.regClass() == v2) {
         Temp high = bld.vop2(aco_opcode::v_ashrrev_i32, bld.def(v1), Operand::c32(31u), tmp);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), tmp, high);
      } else {
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), tmp, Operand::zero());
      }
   }

   return dst;
}

Temp
convert_pointer_to_64_bit(isel_context* ctx, Temp ptr, bool non_uniform)
{
   if (ptr.size() == 2)
      return ptr;
   Builder bld(ctx->program, ctx->block);
   if (ptr.type() == RegType::vgpr && !non_uniform)
      ptr = bld.as_uniform(ptr);
   return bld.pseudo(aco_opcode::p_create_vector, bld.def(RegClass(ptr.type(), 2)), ptr,
                     Operand::c32((unsigned)ctx->options->address32_hi));
}

void
select_vec2(isel_context* ctx, Temp dst, Temp cond, Temp then, Temp els)
{
   Builder bld(ctx->program, ctx->block);

   Temp then_lo = bld.tmp(v1), then_hi = bld.tmp(v1);
   bld.pseudo(aco_opcode::p_split_vector, Definition(then_lo), Definition(then_hi), then);
   Temp else_lo = bld.tmp(v1), else_hi = bld.tmp(v1);
   bld.pseudo(aco_opcode::p_split_vector, Definition(else_lo), Definition(else_hi), els);

   Temp dst0 = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), else_lo, then_lo, cond);
   Temp dst1 = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), else_hi, then_hi, cond);

   bld.pseudo(aco_opcode::p_create_vector, Definition(dst), dst0, dst1);
}

Operand
load_lds_size_m0(Builder& bld)
{
   /* m0 does not need to be initialized on GFX9+ */
   if (bld.program->gfx_level >= GFX9)
      return Operand(s1);

   return bld.m0((Temp)bld.copy(bld.def(s1, m0), Operand::c32(0xffffffffu)));
}

Temp
create_vec_from_array(isel_context* ctx, Temp arr[], unsigned cnt, RegType reg_type,
                      unsigned elem_size_bytes, unsigned split_cnt, Temp dst)
{
   Builder bld(ctx->program, ctx->block);
   unsigned dword_size = elem_size_bytes / 4;

   if (!dst.id())
      dst = bld.tmp(RegClass(reg_type, cnt * dword_size));

   std::array<Temp, NIR_MAX_VEC_COMPONENTS> allocated_vec;
   aco_ptr<Instruction> instr{
      create_instruction(aco_opcode::p_create_vector, Format::PSEUDO, cnt, 1)};
   instr->definitions[0] = Definition(dst);

   for (unsigned i = 0; i < cnt; ++i) {
      if (arr[i].id()) {
         assert(arr[i].size() == dword_size);
         allocated_vec[i] = arr[i];
         instr->operands[i] = Operand(arr[i]);
      } else {
         Temp zero = bld.copy(bld.def(RegClass(reg_type, dword_size)),
                              Operand::zero(dword_size == 2 ? 8 : 4));
         allocated_vec[i] = zero;
         instr->operands[i] = Operand(zero);
      }
   }

   bld.insert(std::move(instr));

   if (split_cnt)
      emit_split_vector(ctx, dst, split_cnt);
   else
      ctx->allocated_vec.emplace(dst.id(), allocated_vec); /* emit_split_vector already does this */

   return dst;
}

void
emit_interp_instr_gfx11(isel_context* ctx, unsigned idx, unsigned component, Temp src, Temp dst,
                        Temp prim_mask, bool high_16bits)
{
   Temp coord1 = emit_extract_vector(ctx, src, 0, v1);
   Temp coord2 = emit_extract_vector(ctx, src, 1, v1);

   Builder bld(ctx->program, ctx->block);

   if (ctx->cf_info.in_divergent_cf || ctx->cf_info.had_divergent_discard) {
      bld.pseudo(aco_opcode::p_interp_gfx11, Definition(dst), Operand(v1.as_linear()),
                 Operand::c32(idx), Operand::c32(component), Operand::c32(high_16bits), coord1,
                 coord2, bld.m0(prim_mask));
      return;
   }

   Temp p = bld.ldsdir(aco_opcode::lds_param_load, bld.def(v1), bld.m0(prim_mask), idx, component);

   Temp res;
   if (dst.regClass() == v2b) {
      Temp p10 = bld.vinterp_inreg(aco_opcode::v_interp_p10_f16_f32_inreg, bld.def(v1), p, coord1,
                                   p, high_16bits ? 0x5 : 0);
      bld.vinterp_inreg(aco_opcode::v_interp_p2_f16_f32_inreg, Definition(dst), p, coord2, p10,
                        high_16bits ? 0x1 : 0);
   } else {
      Temp p10 = bld.vinterp_inreg(aco_opcode::v_interp_p10_f32_inreg, bld.def(v1), p, coord1, p);
      bld.vinterp_inreg(aco_opcode::v_interp_p2_f32_inreg, Definition(dst), p, coord2, p10);
   }
   /* lds_param_load must be done in WQM, and the result kept valid for helper lanes. */
   set_wqm(ctx, true);
}

void
emit_interp_instr(isel_context* ctx, unsigned idx, unsigned component, Temp src, Temp dst,
                  Temp prim_mask, bool high_16bits)
{
   if (ctx->options->gfx_level >= GFX11) {
      emit_interp_instr_gfx11(ctx, idx, component, src, dst, prim_mask, high_16bits);
      return;
   }

   Temp coord1 = emit_extract_vector(ctx, src, 0, v1);
   Temp coord2 = emit_extract_vector(ctx, src, 1, v1);

   Builder bld(ctx->program, ctx->block);

   if (dst.regClass() == v2b) {
      if (ctx->program->dev.has_16bank_lds) {
         assert(ctx->options->gfx_level <= GFX8);
         Builder::Result interp_p1 =
            bld.vintrp(aco_opcode::v_interp_mov_f32, bld.def(v1), Operand::c32(2u) /* P0 */,
                       bld.m0(prim_mask), idx, component);
         interp_p1 = bld.vintrp(aco_opcode::v_interp_p1lv_f16, bld.def(v1), coord1,
                                bld.m0(prim_mask), interp_p1, idx, component, high_16bits);
         bld.vintrp(aco_opcode::v_interp_p2_legacy_f16, Definition(dst), coord2, bld.m0(prim_mask),
                    interp_p1, idx, component, high_16bits);
      } else {
         aco_opcode interp_p2_op = aco_opcode::v_interp_p2_f16;

         if (ctx->options->gfx_level == GFX8)
            interp_p2_op = aco_opcode::v_interp_p2_legacy_f16;

         Builder::Result interp_p1 = bld.vintrp(aco_opcode::v_interp_p1ll_f16, bld.def(v1), coord1,
                                                bld.m0(prim_mask), idx, component, high_16bits);
         bld.vintrp(interp_p2_op, Definition(dst), coord2, bld.m0(prim_mask), interp_p1, idx,
                    component, high_16bits);
      }
   } else {
      assert(!high_16bits);
      Temp interp_p1 = bld.vintrp(aco_opcode::v_interp_p1_f32, bld.def(v1), coord1,
                                  bld.m0(prim_mask), idx, component);

      bld.vintrp(aco_opcode::v_interp_p2_f32, Definition(dst), coord2, bld.m0(prim_mask), interp_p1,
                 idx, component);
   }
}

void
emit_interp_mov_instr(isel_context* ctx, unsigned idx, unsigned component, unsigned vertex_id,
                      Temp dst, Temp prim_mask, bool high_16bits)
{
   Builder bld(ctx->program, ctx->block);
   Temp tmp = dst.bytes() == 2 ? bld.tmp(v1) : dst;
   if (ctx->options->gfx_level >= GFX11) {
      uint16_t dpp_ctrl = dpp_quad_perm(vertex_id, vertex_id, vertex_id, vertex_id);
      if (ctx->cf_info.in_divergent_cf || ctx->cf_info.had_divergent_discard) {
         bld.pseudo(aco_opcode::p_interp_gfx11, Definition(tmp), Operand(v1.as_linear()),
                    Operand::c32(idx), Operand::c32(component), Operand::c32(dpp_ctrl),
                    bld.m0(prim_mask));
      } else {
         Temp p =
            bld.ldsdir(aco_opcode::lds_param_load, bld.def(v1), bld.m0(prim_mask), idx, component);
         bld.vop1_dpp(aco_opcode::v_mov_b32, Definition(tmp), p, dpp_ctrl);
         /* lds_param_load must be done in WQM, and the result kept valid for helper lanes. */
         set_wqm(ctx, true);
      }
   } else {
      bld.vintrp(aco_opcode::v_interp_mov_f32, Definition(tmp), Operand::c32((vertex_id + 2) % 3),
                 bld.m0(prim_mask), idx, component);
   }

   if (dst.id() != tmp.id())
      bld.pseudo(aco_opcode::p_extract_vector, Definition(dst), tmp, Operand::c32(high_16bits));
}

/* Packs multiple Temps of different sizes in to a vector of v1 Temps.
 * The byte count of each input Temp must be a multiple of 2.
 */
std::vector<Temp>
emit_pack_v1(isel_context* ctx, const std::vector<Temp>& unpacked)
{
   Builder bld(ctx->program, ctx->block);
   std::vector<Temp> packed;
   Temp low = Temp();
   for (Temp tmp : unpacked) {
      assert(tmp.bytes() % 2 == 0);
      unsigned byte_idx = 0;
      while (byte_idx < tmp.bytes()) {
         if (low != Temp()) {
            Temp high = emit_extract_vector(ctx, tmp, byte_idx / 2, v2b);
            Temp dword = bld.pseudo(aco_opcode::p_create_vector, bld.def(v1), low, high);
            low = Temp();
            packed.push_back(dword);
            byte_idx += 2;
         } else if (byte_idx % 4 == 0 && (byte_idx + 4) <= tmp.bytes()) {
            packed.emplace_back(emit_extract_vector(ctx, tmp, byte_idx / 4, v1));
            byte_idx += 4;
         } else {
            low = emit_extract_vector(ctx, tmp, byte_idx / 2, v2b);
            byte_idx += 2;
         }
      }
   }
   if (low != Temp()) {
      Temp dword = bld.pseudo(aco_opcode::p_create_vector, bld.def(v1), low, Operand(v2b));
      packed.push_back(dword);
   }
   return packed;
}

MIMG_instruction*
emit_mimg(Builder& bld, aco_opcode op, std::vector<Temp> dsts, Temp rsrc, Operand samp,
          std::vector<Temp> coords, bool disable_wqm, Operand vdata)
{
   bool is_vsample = !samp.isUndefined() || op == aco_opcode::image_msaa_load;

   size_t nsa_size = bld.program->dev.max_nsa_vgprs;
   if (!is_vsample && bld.program->gfx_level >= GFX12)
      nsa_size++; /* VIMAGE can encode one more VADDR */
   nsa_size = bld.program->gfx_level >= GFX11 || coords.size() <= nsa_size ? nsa_size : 0;

   const bool strict_wqm = coords[0].regClass().is_linear_vgpr();
   if (strict_wqm)
      nsa_size = coords.size();

   for (unsigned i = 0; i < std::min(coords.size(), nsa_size); i++) {
      if (!coords[i].id())
         continue;

      coords[i] = as_vgpr(bld, coords[i]);
   }

   if (nsa_size < coords.size()) {
      Temp coord = coords[nsa_size];
      if (coords.size() - nsa_size > 1) {
         aco_ptr<Instruction> vec{create_instruction(aco_opcode::p_create_vector, Format::PSEUDO,
                                                     coords.size() - nsa_size, 1)};

         unsigned coord_size = 0;
         for (unsigned i = nsa_size; i < coords.size(); i++) {
            vec->operands[i - nsa_size] = Operand(coords[i]);
            coord_size += coords[i].size();
         }

         coord = bld.tmp(RegType::vgpr, coord_size);
         vec->definitions[0] = Definition(coord);
         bld.insert(std::move(vec));
      } else {
         coord = as_vgpr(bld, coord);
      }

      coords[nsa_size] = coord;
      coords.resize(nsa_size + 1);
   }

   aco_ptr<Instruction> mimg{
      create_instruction(op, Format::MIMG, 3 + coords.size() + disable_wqm * 2, dsts.size())};
   for (unsigned i = 0; i < dsts.size(); ++i)
      mimg->definitions[i] = Definition(dsts[i]);
   mimg->operands[0] = Operand(rsrc);
   mimg->operands[1] = samp;
   mimg->operands[2] = vdata;
   for (unsigned i = 0; i < coords.size(); i++)
      mimg->operands[3 + i] = Operand(coords[i]);

   init_disable_wqm(bld, mimg->mimg(), disable_wqm);
   mimg->mimg().strict_wqm = strict_wqm;

   return &bld.insert(std::move(mimg))->mimg();
}

Operand
emit_tfe_init(Builder& bld, Temp dst)
{
   Temp tmp = bld.tmp(dst.regClass());

   aco_ptr<Instruction> vec{
      create_instruction(aco_opcode::p_create_vector, Format::PSEUDO, dst.size(), 1)};
   for (unsigned i = 0; i < dst.size(); i++)
      vec->operands[i] = Operand::zero();
   vec->definitions[0] = Definition(tmp);
   /* Since this is fixed to an instruction's definition register, any CSE will
    * just create copies. Copying costs about the same as zero-initialization,
    * but these copies can break up clauses.
    */
   vec->definitions[0].setNoCSE(true);
   bld.insert(std::move(vec));

   return Operand(tmp);
}

void
create_fs_dual_src_export_gfx11(isel_context* ctx, const struct aco_export_mrt* mrt0,
                                const struct aco_export_mrt* mrt1)
{
   Builder bld(ctx->program, ctx->block);

   aco_ptr<Instruction> exp{
      create_instruction(aco_opcode::p_dual_src_export_gfx11, Format::PSEUDO, 10, 6)};
   for (unsigned i = 0; i < 4; i++) {
      exp->operands[i] = mrt0 ? mrt0->out[i] : Operand(v1);
      exp->operands[i + 4] = mrt1 ? mrt1->out[i] : Operand(v1);
   }

   instr_exact_mask(exp.get()) = Operand();
   instr_wqm_mask(exp.get()) = Operand();

   RegClass type = RegClass(RegType::vgpr, util_bitcount(mrt0->enabled_channels));
   exp->definitions[0] = bld.def(type); /* mrt0 */
   exp->definitions[1] = bld.def(type); /* mrt1 */
   exp->definitions[2] = bld.def(bld.lm);
   exp->definitions[3] = bld.def(bld.lm);
   exp->definitions[4] = bld.def(bld.lm, vcc);
   exp->definitions[5] = bld.def(s1, scc);
   ctx->block->instructions.emplace_back(std::move(exp));

   ctx->program->has_color_exports = true;
}

Temp
lanecount_to_mask(isel_context* ctx, Temp count, unsigned bit_offset)
{
   assert(count.regClass() == s1);

   Builder bld(ctx->program, ctx->block);

   /* We could optimize other cases, but they are unused at the moment. */
   if (bit_offset != 0 && bit_offset != 8) {
      assert(bit_offset < 32);
      count = bld.sop2(aco_opcode::s_lshr_b32, bld.def(s1), bld.def(s1, scc), count,
                       Operand::c32(bit_offset));
      bit_offset = 0;
   }

   if (ctx->program->wave_size == 32 && bit_offset == 0) {
      /* We use s_bfm_b64 (not _b32) which works with 32, but we need to extract the lower half of
       * the register. It doesn't work for 64 because it only uses 6 bits. */
      Temp mask = bld.sop2(aco_opcode::s_bfm_b64, bld.def(s2), count, Operand::zero());
      return emit_extract_vector(ctx, mask, 0, bld.lm);
   } else {
      /* s_bfe (both u32 and u64) uses 7 bits for the size, but it needs them in the high word.
       * The low word is used for the offset, which has to be zero for our use case.
       */
      if (bit_offset == 0 && ctx->program->gfx_level >= GFX9) {
         /* Avoid writing scc for better scheduling. */
         count = bld.sop2(aco_opcode::s_pack_ll_b32_b16, bld.def(s1), Operand::c32(0), count);
      } else {
         count = bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1), bld.def(s1, scc), count,
                          Operand::c32(16 - bit_offset));
      }

      if (ctx->program->wave_size == 32) {
         return bld.sop2(aco_opcode::s_bfe_u32, bld.def(bld.lm), bld.def(s1, scc), Operand::c32(-1),
                         count);
      } else {
         return bld.sop2(aco_opcode::s_bfe_u64, bld.def(bld.lm), bld.def(s1, scc),
                         Operand::c64(-1ll), count);
      }
   }
}

void
build_end_with_regs(isel_context* ctx, std::vector<Operand>& regs)
{
   aco_ptr<Instruction> end{
      create_instruction(aco_opcode::p_end_with_regs, Format::PSEUDO, regs.size(), 0)};

   for (unsigned i = 0; i < regs.size(); i++)
      end->operands[i] = regs[i];

   ctx->block->instructions.emplace_back(std::move(end));

   ctx->block->kind |= block_kind_end_with_regs;
}

Instruction*
add_startpgm(struct isel_context* ctx)
{
   unsigned def_count = 0;
   for (unsigned i = 0; i < ctx->args->arg_count; i++) {
      if (ctx->args->args[i].skip)
         continue;
      unsigned align = MIN2(4, util_next_power_of_two(ctx->args->args[i].size));
      if (ctx->args->args[i].file == AC_ARG_SGPR && ctx->args->args[i].offset % align)
         def_count += ctx->args->args[i].size;
      else
         def_count++;
   }

   if (ctx->stage.hw == AC_HW_COMPUTE_SHADER && ctx->program->gfx_level >= GFX12)
      def_count += 3;

   Instruction* startpgm = create_instruction(aco_opcode::p_startpgm, Format::PSEUDO, 0, def_count);
   ctx->block->instructions.emplace_back(startpgm);
   for (unsigned i = 0, arg = 0; i < ctx->args->arg_count; i++) {
      if (ctx->args->args[i].skip)
         continue;

      enum ac_arg_regfile file = ctx->args->args[i].file;
      unsigned size = ctx->args->args[i].size;
      unsigned reg = ctx->args->args[i].offset;
      RegClass type = RegClass(file == AC_ARG_SGPR ? RegType::sgpr : RegType::vgpr, size);

      if (file == AC_ARG_SGPR && reg % MIN2(4, util_next_power_of_two(size))) {
         Temp elems[16];
         for (unsigned j = 0; j < size; j++) {
            elems[j] = ctx->program->allocateTmp(s1);
            startpgm->definitions[arg++] = Definition(elems[j], PhysReg{reg + j});
         }
         ctx->arg_temps[i] = create_vec_from_array(ctx, elems, size, RegType::sgpr, 4);
      } else {
         Temp dst = ctx->program->allocateTmp(type);
         Definition def(dst);
         def.setPrecolored(PhysReg{file == AC_ARG_SGPR ? reg : reg + 256});
         ctx->arg_temps[i] = dst;
         startpgm->definitions[arg++] = def;

         if (ctx->args->args[i].pending_vmem) {
            assert(file == AC_ARG_VGPR);
            ctx->program->args_pending_vmem.push_back(def);
         }
      }
   }

   if (ctx->program->gfx_level >= GFX12 && ctx->stage.hw == AC_HW_COMPUTE_SHADER) {
      Temp idx = ctx->program->allocateTmp(s1);
      Temp idy = ctx->program->allocateTmp(s1);
      ctx->ttmp8 = ctx->program->allocateTmp(s1);
      startpgm->definitions[def_count - 3] = Definition(idx);
      startpgm->definitions[def_count - 3].setPrecolored(PhysReg(108 + 9 /*ttmp9*/));
      startpgm->definitions[def_count - 2] = Definition(ctx->ttmp8);
      startpgm->definitions[def_count - 2].setPrecolored(PhysReg(108 + 8 /*ttmp8*/));
      startpgm->definitions[def_count - 1] = Definition(idy);
      startpgm->definitions[def_count - 1].setPrecolored(PhysReg(108 + 7 /*ttmp7*/));
      ctx->workgroup_id[0] = Operand(idx);
      if (ctx->args->workgroup_ids[2].used) {
         Builder bld(ctx->program, ctx->block);
         ctx->workgroup_id[1] =
            bld.pseudo(aco_opcode::p_extract, bld.def(s1), bld.def(s1, scc), idy, Operand::zero(),
                       Operand::c32(16u), Operand::zero());
         ctx->workgroup_id[2] =
            bld.pseudo(aco_opcode::p_extract, bld.def(s1), bld.def(s1, scc), idy, Operand::c32(1u),
                       Operand::c32(16u), Operand::zero());
      } else {
         ctx->workgroup_id[1] = Operand(idy);
         ctx->workgroup_id[2] = Operand::zero();
      }
   } else if (ctx->stage.hw == AC_HW_COMPUTE_SHADER) {
      const struct ac_arg* ids = ctx->args->workgroup_ids;
      for (unsigned i = 0; i < 3; i++)
         ctx->workgroup_id[i] = ids[i].used ? Operand(get_arg(ctx, ids[i])) : Operand::zero();
   }

   /* epilog has no scratch */
   if (ctx->args->scratch_offset.used) {
      if (ctx->program->gfx_level < GFX9) {
         /* Stash these in the program so that they can be accessed later when
          * handling spilling.
          */
         if (ctx->args->ring_offsets.used)
            ctx->program->private_segment_buffers.push_back(get_arg(ctx, ctx->args->ring_offsets));

         ctx->program->scratch_offsets.push_back(get_arg(ctx, ctx->args->scratch_offset));
      } else if (ctx->program->gfx_level <= GFX10_3 && ctx->program->stage != raytracing_cs) {
         /* Manually initialize scratch. For RT stages scratch initialization is done in the prolog.
          */
         Operand scratch_addr = ctx->args->ring_offsets.used
                                   ? Operand(get_arg(ctx, ctx->args->ring_offsets))
                                   : Operand(s2);

         Builder bld(ctx->program, ctx->block);
         bld.pseudo(aco_opcode::p_init_scratch, bld.def(s2), bld.def(s1, scc), scratch_addr,
                    get_arg(ctx, ctx->args->scratch_offset));
      }
   }

   return startpgm;
}

static void
cleanup_cfg(Program* program)
{
   /* create linear_succs/logical_succs */
   for (Block& BB : program->blocks) {
      for (unsigned idx : BB.linear_preds)
         program->blocks[idx].linear_succs.emplace_back(BB.index);
      for (unsigned idx : BB.logical_preds)
         program->blocks[idx].logical_succs.emplace_back(BB.index);
   }
}

void
finish_program(isel_context* ctx)
{
   cleanup_cfg(ctx->program);

   /* Insert a single p_end_wqm instruction after the last derivative calculation */
   if (ctx->program->stage == fragment_fs && ctx->program->needs_wqm && ctx->program->needs_exact) {
      /* Find the next BB at top-level CFG */
      while (!(ctx->program->blocks[ctx->wqm_block_idx].kind & block_kind_top_level)) {
         ctx->wqm_block_idx++;
         ctx->wqm_instruction_idx = 0;
      }

      std::vector<aco_ptr<Instruction>>* instrs =
         &ctx->program->blocks[ctx->wqm_block_idx].instructions;
      auto it = instrs->begin() + ctx->wqm_instruction_idx;

      /* Delay transistion to Exact to help optimizations and scheduling */
      while (it != instrs->end()) {
         aco_ptr<Instruction>& instr = *it;
         /* End WQM before: */
         if (instr->isDS() || instr->isEXP() ||
             instr->opcode == aco_opcode::p_dual_src_export_gfx11 ||
             instr->opcode == aco_opcode::p_jump_to_epilog ||
             instr->opcode == aco_opcode::p_logical_start)
            break;

         ++it;

         /* End WQM after: */
         if (instr->opcode == aco_opcode::p_logical_end ||
             instr->opcode == aco_opcode::p_discard_if ||
             instr->opcode == aco_opcode::p_demote_to_helper ||
             instr->opcode == aco_opcode::p_end_with_regs)
            break;
      }

      Builder bld(ctx->program);
      bld.reset(instrs, it);
      bld.pseudo(aco_opcode::p_end_wqm);
   }
}

struct param_assignment_info {
   uint16_t required_alignment;
   uint16_t provided_alignment;
   RegClass rc;
   parameter_info* dst_info;
   bool is_return_param;
   /* If true, this parameter shouldn't count toward the callee info's reg_param_count because it
    * receives special handling (e.g. the call return address being a definition instead of an
    * operand).
    */
   bool is_system_param;
   /* This parameter must reside in a register. Used for stack pointers as well as s_swappc
    * operands.
    */
   bool force_reg;
};

std::optional<PhysReg>
find_reg(BITSET_WORD* regs, RegClass rc)
{
   uint16_t start = 0;
   uint16_t size = 128;
   if (rc.type() == RegType::vgpr) {
      start = 256;
      size = 256;
   }

   uint16_t contiguous_size = 0;
   for (uint16_t i = 0; i < size; ++i) {
      if (!BITSET_TEST(regs, start + i)) {
         contiguous_size = 0;
         continue;
      }
      if (++contiguous_size >= rc.size())
         return PhysReg{(unsigned)(start + i - contiguous_size + 1)};
   }
   return {};
}

void
find_param_regs(Program* program, const ABI& abi, callee_info& info,
                std::vector<struct param_assignment_info>& params, RegisterDemand reg_limit)
{
   unsigned scratch_param_bytes = 0;
   RegisterDemand param_demand = RegisterDemand();

   BITSET_DECLARE(preserved_regs, 512);
   BITSET_DECLARE(clobbered_regs, 512);
   abi.preservedRegisters(preserved_regs, reg_limit);
   BITSET_COPY(clobbered_regs, preserved_regs);
   BITSET_NOT(clobbered_regs);
   bool has_preserved_regs = !BITSET_IS_EMPTY(preserved_regs);

   std::stable_sort(params.begin(), params.end(),
                    [](const param_assignment_info& first, const param_assignment_info& second)
                    {
                       /* Assign parameters with larger alignments first so we can use parameters
                        * with smaller alignments as padding
                        */
                       return first.provided_alignment > second.provided_alignment;
                    });
   std::stable_sort(params.begin(), params.end(),
                    [](const param_assignment_info& first, const param_assignment_info& second)
                    {
                       /* Move parameters forced into registers to the very front so we assign
                        * them first.
                        */
                       return first.force_reg && !second.force_reg;
                    });
   for (size_t i = 1; i < params.size(); ++i) {
      assert(!params[i].force_reg || params[i - 1].force_reg);
   }
   /* Reverse parameters and start from the end, to make erasing elements cheap */
   std::reverse(params.begin(), params.end());

   while (!params.empty()) {
      RegClass rc = params.back().rc;
      bool discardable = params.back().dst_info->discardable || params.back().is_return_param;

      BITSET_WORD* regs;
      if (has_preserved_regs && !discardable)
         regs = preserved_regs;
      else
         regs = clobbered_regs;

      auto next_reg = find_reg(regs, rc);
      /* Force parameter into scratch if it exceeds the ABI's maximum parameter demand */
      if (abi.max_param_demand != RegisterDemand() &&
          (param_demand + Temp(0, rc)).exceeds(abi.max_param_demand))
         next_reg = {};

      if (next_reg && next_reg->reg() % params.back().required_alignment) {
         /* We found a register, but it's not aligned properly. Check if we can add some padding
          * (and ideally stuff a different parameter in there).
          */
         uint16_t required_padding =
            params.back().required_alignment - (next_reg->reg() % params.back().required_alignment);
         uint16_t aligned_size = rc.size() + required_padding;
         for (unsigned i = 0; i < aligned_size; ++i) {
            /* The added padding exceeds the size of the register range. Just bail out at this
             * point.
             * TODO: we could probably try finding a new register, but then we'd need to reevaluate
             * alignment etc...
             */
            if (!BITSET_TEST(regs, next_reg->advance(i * 4).reg())) {
               next_reg = {};
               break;
            }
         }

         /* Try finding a small parameter to put inside the padding space */
         for (auto it2 = std::next(params.rbegin()); next_reg && it2 != params.rend(); ++it2) {
            if (it2->rc.type() != params.back().rc.type() ||
                it2->dst_info->discardable != discardable)
               continue;
            if (it2->rc.size() > required_padding || (it2->required_alignment % next_reg->reg()))
               continue;

            param_demand += Temp(0, it2->rc);

            it2->dst_info->def.setPrecolored(*next_reg);
            for (unsigned i = 0; i < it2->rc.size(); ++i)
               BITSET_CLEAR(regs, next_reg->reg() + i);
            if (!it2->is_system_param) {
               ++info.reg_param_count;
               if (discardable)
                  ++info.reg_discardable_param_count;
            }
            params.erase(std::prev(it2.base()));
            break;
         }
         if (next_reg)
            next_reg = next_reg->advance(required_padding * 4);
      }
      if (next_reg) {
         param_demand += Temp(0, params.back().rc);
         params.back().dst_info->def.setPrecolored(*next_reg);
         BITSET_CLEAR_RANGE(regs, next_reg->reg(), next_reg->reg() + params.back().rc.size() - 1);
         if (!params.back().is_system_param) {
            ++info.reg_param_count;
            if (discardable)
               ++info.reg_discardable_param_count;
         }
      } else {
         assert(!params.back().force_reg);
         params.back().dst_info->is_reg = false;
         params.back().dst_info->scratch_offset = scratch_param_bytes;
         scratch_param_bytes += rc.size() * 4;
      }
      params.pop_back();
   }

   info.scratch_param_size = scratch_param_bytes;
   if (program)
      program->callee_param_demand = param_demand;
}

struct callee_info
get_callee_info(amd_gfx_level gfx_level, const ABI& abi, unsigned param_count,
                const nir_parameter* parameters, Program* program, RegisterDemand reg_limit)
{
   struct callee_info info = {};
   info.param_infos.reserve(param_count);

   std::vector<param_assignment_info> assignment_infos;
   assignment_infos.reserve(param_count + 2);

   Temp return_addr = program ? program->allocateTmp(s2) : Temp();
   Definition return_def = Definition(return_addr);
   info.return_address = {};
   info.return_address.discardable = false;
   info.return_address.is_reg = true;
   info.return_address.def = return_def;

   param_assignment_info return_def_info = {};
   return_def_info.required_alignment = 2;
   return_def_info.provided_alignment = 2;
   return_def_info.rc = s2;
   return_def_info.dst_info = &info.return_address;
   return_def_info.is_return_param = false;
   return_def_info.is_system_param = true;
   return_def_info.force_reg = true;
   assignment_infos.push_back(return_def_info);

   if (gfx_level >= GFX9) {
      Temp stack_ptr = program ? program->allocateTmp(s1) : Temp();
      Definition stack_def = Definition(stack_ptr);
      info.stack_ptr = {};
      info.stack_ptr.discardable = false;
      info.stack_ptr.is_reg = true;
      info.stack_ptr.def = stack_def;

      param_assignment_info stack_ptr_info = {};
      stack_ptr_info.required_alignment = 1;
      stack_ptr_info.provided_alignment = 1;
      stack_ptr_info.rc = s1;
      stack_ptr_info.dst_info = &info.stack_ptr;
      stack_ptr_info.is_return_param = false;
      stack_ptr_info.is_system_param = true;
      stack_ptr_info.force_reg = true;
      assignment_infos.push_back(stack_ptr_info);
   } else {
      Temp scratch_rsrc = program ? program->allocateTmp(s4) : Temp();
      Definition rsrc_def = Definition(scratch_rsrc);
      info.stack_ptr = {};
      info.stack_ptr.discardable = false;
      info.stack_ptr.is_reg = true;
      info.stack_ptr.def = rsrc_def;

      param_assignment_info rsrc_info = {};
      rsrc_info.required_alignment = 4;
      rsrc_info.provided_alignment = 4;
      rsrc_info.rc = s4;
      rsrc_info.dst_info = &info.stack_ptr;
      rsrc_info.is_return_param = false;
      rsrc_info.is_system_param = true;
      rsrc_info.force_reg = true;
      assignment_infos.push_back(rsrc_info);
   }

   size_t info_base = assignment_infos.size();

   for (unsigned i = 0; i < param_count; ++i) {
      RegType type = parameters[i].is_uniform ? RegType::sgpr : RegType::vgpr;
      unsigned byte_size = align(parameters[i].bit_size, 32) / 8 * parameters[i].num_components;
      RegClass rc = RegClass(type, byte_size / 4);

      Temp dst = program ? program->allocateTmp(rc) : Temp();
      Definition def = Definition(dst);

      parameter_info param_info = {};
      param_info.discardable =
         !!(parameters[i].driver_attributes & ACO_NIR_PARAM_ATTRIB_DISCARDABLE);
      param_info.is_reg = true;
      param_info.def = def;
      info.param_infos.push_back(param_info);

      uint16_t required_alignment = 1;
      uint16_t provided_alignment = 1;

      if (rc.type() == RegType::sgpr) {
         if (rc.size() > 2)
            required_alignment = 4;
         else if (rc.size() > 1)
            required_alignment = 2;
      }
      if (rc.size() % 4 == 0)
         provided_alignment = 4;
      else if (rc.size() % 2 == 0)
         provided_alignment = 2;

      param_assignment_info assignment_info = {};
      assignment_info.required_alignment = required_alignment;
      assignment_info.provided_alignment = provided_alignment;
      assignment_info.rc = rc;
      assignment_info.is_return_param = parameters[i].is_return;
      /* Force the first two parameters (callee addresses) into registers - they're assumed to be
       * accessible through a temp.
       */
      assignment_info.force_reg = i <= 1;
      assignment_infos.push_back(assignment_info);
   }

   for (unsigned i = 0; i < param_count; ++i)
      assignment_infos[info_base + i].dst_info = &info.param_infos[i];

   find_param_regs(program, abi, info, assignment_infos, reg_limit);

   return info;
}

} // namespace aco
