/*
 * Copyright © 2018 Valve Corporation
 * Copyright © 2018 Google
 *
 * SPDX-License-Identifier: MIT
 */

#include "aco_builder.h"
#include "aco_instruction_selection.h"
#include "aco_ir.h"

#include "nir_range_analysis.h"

#include "ac_descriptors.h"
#include "ac_nir.h"
#include "amdgfxregs.h"
#include <numeric>
#include <stdint.h>

namespace aco {
namespace {

Temp
emit_mbcnt(isel_context* ctx, Temp dst, Operand mask = Operand(), Operand base = Operand::zero())
{
   Builder bld(ctx->program, ctx->block);
   assert(mask.isUndefined() || mask.isTemp() || (mask.isFixed() && mask.physReg() == exec));
   assert(mask.isUndefined() || mask.bytes() == bld.lm.bytes());

   if (ctx->program->wave_size == 32) {
      Operand mask_lo = mask.isUndefined() ? Operand::c32(-1u) : mask;
      return bld.vop3(aco_opcode::v_mbcnt_lo_u32_b32, Definition(dst), mask_lo, base);
   }

   Operand mask_lo = Operand::c32(-1u);
   Operand mask_hi = Operand::c32(-1u);

   if (mask.isTemp()) {
      RegClass rc = RegClass(mask.regClass().type(), 1);
      Builder::Result mask_split =
         bld.pseudo(aco_opcode::p_split_vector, bld.def(rc), bld.def(rc), mask);
      mask_lo = Operand(mask_split.def(0).getTemp());
      mask_hi = Operand(mask_split.def(1).getTemp());
   } else if (mask.physReg() == exec) {
      mask_lo = Operand(exec_lo, s1);
      mask_hi = Operand(exec_hi, s1);
   }

   Temp mbcnt_lo = bld.vop3(aco_opcode::v_mbcnt_lo_u32_b32, bld.def(v1), mask_lo, base);

   if (ctx->program->gfx_level <= GFX7)
      return bld.vop2(aco_opcode::v_mbcnt_hi_u32_b32, Definition(dst), mask_hi, mbcnt_lo);
   else
      return bld.vop3(aco_opcode::v_mbcnt_hi_u32_b32_e64, Definition(dst), mask_hi, mbcnt_lo);
}

bool
can_use_shared_vgprs(isel_context* ctx)
{
   /* Avoid using shared VGPRs for shuffle on GFX10 when the shader consists
    * of multiple binaries, because the VGPR use is not known when choosing
    * which registers to use for the shared VGPRs.
    */
   return ctx->options->gfx_level >= GFX10 && ctx->options->gfx_level < GFX11 &&
          ctx->program->wave_size == 64 && !ctx->program->info.ps.has_epilog &&
          !ctx->program->info.merged_shader_compiled_separately &&
          !ctx->program->info.vs.has_prolog && ctx->stage != raytracing_cs;
}

void
enable_shared_vgprs(isel_context* ctx)
{
   assert(can_use_shared_vgprs(ctx));
   if (ctx->program->config->num_shared_vgprs)
      return;

   /* We need one pair of shared VGPRs:
    * Note, that these have twice the allocation granularity of normal VGPRs
    */
   ctx->program->config->num_shared_vgprs = 2 * ctx->program->dev.vgpr_alloc_granule;
}

Temp
emit_bpermute(isel_context* ctx, Builder& bld, Temp index, Temp data)
{
   if (index.regClass() == s1)
      return bld.readlane(bld.def(s1), data, index);

   if ((ctx->options->gfx_level >= GFX8 && ctx->options->gfx_level < GFX10) ||
       ctx->options->gfx_level >= GFX12 || ctx->program->wave_size == 32) {
      /* wave32 or GFX8-9, GFX12+: bpermute works normally */
      Temp index_x4 = bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand::c32(2u), index);
      return bld.ds(aco_opcode::ds_bpermute_b32, bld.def(v1), index_x4, data);
   } else if (ctx->options->gfx_level >= GFX11 || can_use_shared_vgprs(ctx)) {
      /* GFX10-11.5 wave64 mode: emulate full-wave bpermute */
      Temp index_is_lo =
         bld.vopc(aco_opcode::v_cmp_ge_u32, bld.def(bld.lm), Operand::c32(31u), index);
      Builder::Result index_is_lo_split =
         bld.pseudo(aco_opcode::p_split_vector, bld.def(s1), bld.def(s1), index_is_lo);
      Temp index_is_lo_n1 = bld.sop1(aco_opcode::s_not_b32, bld.def(s1), bld.def(s1, scc),
                                     index_is_lo_split.def(1).getTemp());
      Operand same_half = bld.pseudo(aco_opcode::p_create_vector, bld.def(s2),
                                     index_is_lo_split.def(0).getTemp(), index_is_lo_n1);
      Operand index_x4 = bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand::c32(2u), index);

      if (ctx->options->gfx_level <= GFX10_3) {
         enable_shared_vgprs(ctx);

         return bld.pseudo(aco_opcode::p_bpermute_shared_vgpr, bld.def(v1), bld.def(s2),
                           bld.def(s1, scc), index_x4, data, same_half);
      } else {
         return bld.pseudo(aco_opcode::p_bpermute_permlane, bld.def(v1), bld.def(s2),
                           bld.def(s1, scc), Operand(v1.as_linear()), index_x4, data, same_half);
      }
   } else {
      /* GFX6-7: there is no bpermute instruction */
      return bld.pseudo(aco_opcode::p_bpermute_readlane, bld.def(v1), bld.def(bld.lm),
                        bld.def(bld.lm, vcc), index, data);
   }
}

Temp
emit_masked_swizzle(isel_context* ctx, Builder& bld, Temp src, unsigned mask, bool allow_fi)
{
   if (ctx->options->gfx_level >= GFX8) {
      unsigned and_mask = mask & 0x1f;
      unsigned or_mask = (mask >> 5) & 0x1f;
      unsigned xor_mask = (mask >> 10) & 0x1f;

      /* Eliminate or_mask. */
      and_mask &= ~or_mask;
      xor_mask ^= or_mask;

      uint16_t dpp_ctrl = 0xffff;

      /* DPP16 before DPP8 before v_permlane(x)16_b32
       * because DPP16 supports modifiers and v_permlane
       * can't be folded into valu instructions.
       */
      if ((and_mask & 0x1c) == 0x1c && xor_mask < 4) {
         unsigned res[4];
         for (unsigned i = 0; i < 4; i++)
            res[i] = ((i & and_mask) ^ xor_mask);
         dpp_ctrl = dpp_quad_perm(res[0], res[1], res[2], res[3]);
      } else if (and_mask == 0x1f && xor_mask == 8) {
         dpp_ctrl = dpp_row_rr(8);
      } else if (and_mask == 0x1f && xor_mask == 0xf) {
         dpp_ctrl = dpp_row_mirror;
      } else if (and_mask == 0x1f && xor_mask == 0x7) {
         dpp_ctrl = dpp_row_half_mirror;
      } else if (ctx->options->gfx_level >= GFX11 && and_mask == 0x10 && xor_mask < 0x10) {
         dpp_ctrl = dpp_row_share(xor_mask);
      } else if (ctx->options->gfx_level >= GFX11 && and_mask == 0x1f && xor_mask < 0x10) {
         dpp_ctrl = dpp_row_xmask(xor_mask);
      } else if (ctx->options->gfx_level >= GFX10 && (and_mask & 0x18) == 0x18 && xor_mask < 8) {
         uint32_t lane_sel = 0;
         for (unsigned i = 0; i < 8; i++)
            lane_sel |= ((i & and_mask) ^ xor_mask) << (i * 3);
         return bld.vop1_dpp8(aco_opcode::v_mov_b32, bld.def(v1), src, lane_sel, allow_fi);
      } else if (ctx->options->gfx_level >= GFX10 && (and_mask & 0x10) == 0x10) {
         uint64_t lane_mask = 0;
         for (unsigned i = 0; i < 16; i++)
            lane_mask |= uint64_t((i & and_mask) ^ (xor_mask & 0xf)) << i * 4;
         aco_opcode opcode =
            xor_mask & 0x10 ? aco_opcode::v_permlanex16_b32 : aco_opcode::v_permlane16_b32;
         Temp op1 = bld.copy(bld.def(s1), Operand::c32(lane_mask & 0xffffffff));
         Temp op2 = bld.copy(bld.def(s1), Operand::c32(lane_mask >> 32));
         Builder::Result ret = bld.vop3(opcode, bld.def(v1), src, op1, op2);
         ret->valu().opsel[0] = allow_fi; /* set FETCH_INACTIVE */
         ret->valu().opsel[1] = true;     /* set BOUND_CTRL */
         return ret;
      }

      if (dpp_ctrl != 0xffff)
         return bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(v1), src, dpp_ctrl, 0xf, 0xf, true,
                             allow_fi);
   }

   return bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), src, mask, 0, false);
}

Temp
as_vgpr(Builder& bld, Temp val)
{
   if (val.type() == RegType::sgpr)
      return bld.copy(bld.def(RegType::vgpr, val.size()), val);
   assert(val.type() == RegType::vgpr);
   return val;
}

void
emit_extract_vector(isel_context* ctx, Temp src, uint32_t idx, Temp dst)
{
   Builder bld(ctx->program, ctx->block);
   bld.pseudo(aco_opcode::p_extract_vector, Definition(dst), src, Operand::c32(idx));
}

Temp
emit_vector_as_uniform(isel_context* ctx, Temp src, Temp dst,
                       bool readfirstlane_for_uniform = false)
{
   Builder bld(ctx->program, ctx->block);

   if (src.regClass().type() == RegType::sgpr) {
      bld.copy(Definition(dst), src);
   } else if (src.size() == 1) {
      if (readfirstlane_for_uniform)
         bld.vop1(aco_opcode::v_readfirstlane_b32, Definition(dst), src);
      else
         bld.pseudo(aco_opcode::p_as_uniform, Definition(dst), src);
   } else {
      aco_ptr<Instruction> split{
         create_instruction(aco_opcode::p_split_vector, Format::PSEUDO, 1, src.size())};
      split->operands[0] = Operand(src);

      for (unsigned i = 0; i < src.size(); i++) {
         split->definitions[i] =
            bld.def(RegClass::get(RegType::vgpr, MIN2(src.bytes() - i * 4, 4)));
      }

      Instruction* split_raw = split.get();
      ctx->block->instructions.emplace_back(std::move(split));

      std::array<Temp, NIR_MAX_VEC_COMPONENTS> elems;
      aco_ptr<Instruction> vec{
         create_instruction(aco_opcode::p_create_vector, Format::PSEUDO, src.size(), 1)};
      vec->definitions[0] = Definition(dst);
      for (unsigned i = 0; i < src.size(); i++) {
         if (readfirstlane_for_uniform)
            vec->operands[i] = bld.vop1(aco_opcode::v_readfirstlane_b32, bld.def(s1),
                                        split_raw->definitions[i].getTemp());
         else
            vec->operands[i] = bld.pseudo(aco_opcode::p_as_uniform, bld.def(s1),
                                          split_raw->definitions[i].getTemp());
         elems[i] = vec->operands[i].getTemp();
      }

      ctx->block->instructions.emplace_back(std::move(vec));
      ctx->allocated_vec.emplace(dst.id(), elems);
   }

   return dst;
}

Temp
add64_32(Builder& bld, Temp src0, Operand src1)
{
   Temp src00 = bld.tmp(src0.type(), 1);
   Temp src01 = bld.tmp(src0.type(), 1);
   bld.pseudo(aco_opcode::p_split_vector, Definition(src00), Definition(src01), src0);

   if (src0.type() == RegType::vgpr || src1.isOfType(RegType::vgpr)) {
      src1 = src1.isOfType(RegType::vgpr) ? src1 : bld.copy(bld.def(v1), src1);
      Temp dst0 = bld.tmp(v1);
      Temp carry = bld.vadd32(Definition(dst0), src00, src1, true).def(1).getTemp();
      Temp dst1 = bld.vadd32(bld.def(v1), src01, Operand::zero(), false, carry);
      return bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), dst0, dst1);
   } else {
      Temp carry = bld.tmp(s1);
      Temp dst0 =
         bld.sop2(aco_opcode::s_add_u32, bld.def(s1), bld.scc(Definition(carry)), src00, src1);
      Temp dst1 = bld.sop2(aco_opcode::s_add_u32, bld.def(s1), bld.def(s1, scc), src01, carry);
      return bld.pseudo(aco_opcode::p_create_vector, bld.def(s2), dst0, dst1);
   }
}

/* undef becomes Temp(id=0) */
Temp
as_temp(Builder& bld, Operand op)
{
   RegClass rc(RegType::sgpr, op.size());
   return op.isTemp() ? op.getTemp() : op.isConstant() ? bld.copy(bld.def(rc), op) : Temp(0, rc);
}

struct LoadEmitInfo {
   Operand offset;
   Temp dst;
   unsigned num_components;
   unsigned component_size;
   Temp resource = Temp(0, s1); /* buffer resource or base 64-bit address */
   Temp idx = Temp(0, v1);      /* buffer index */
   unsigned component_stride = 0;
   unsigned const_offset = 0;
   unsigned align_mul = 0;
   unsigned align_offset = 0;
   pipe_format format;
   nir_src* resource_src = NULL; /* should be equal to resource or NULL */
   nir_src* offset_src = NULL; /* should be equal to offset or NULL */
   isel_context* ctx;

   ac_hw_cache_flags cache = {{0, 0, 0, 0, 0}};
   bool split_by_component_stride = true;
   bool disable_wqm = false;
   unsigned swizzle_component_size = 0;
   memory_sync_info sync;
   Temp soffset = Temp(0, s1);
};

struct EmitLoadParameters {
   using Callback = Temp (*)(Builder& bld, const LoadEmitInfo& info, unsigned bytes_needed,
                             unsigned align);

   Callback callback;
   uint32_t max_const_offset;
};

bool
add_might_overflow(isel_context* ctx, nir_src* offset, uint32_t const_offset)
{
   if (!offset)
      return true;
   return nir_addition_might_overflow(ctx->shader, ctx->range_ht, nir_get_scalar(offset->ssa, 0),
                                      const_offset);
}

void
emit_load(isel_context* ctx, Builder& bld, const LoadEmitInfo& info,
          const EmitLoadParameters& params)
{
   unsigned load_size = info.num_components * info.component_size;
   unsigned component_size = info.component_size;

   unsigned num_vals = 0;
   Temp* const vals = (Temp*)alloca(info.dst.bytes() * sizeof(Temp));

   unsigned const_offset = info.const_offset;

   const unsigned align_mul = info.align_mul ? info.align_mul : component_size;
   unsigned align_offset = info.align_offset % align_mul;

   unsigned bytes_read = 0;
   while (bytes_read < load_size) {
      unsigned bytes_needed = load_size - bytes_read;

      if (info.split_by_component_stride) {
         if (info.swizzle_component_size)
            bytes_needed = MIN2(bytes_needed, info.swizzle_component_size);
         if (info.component_stride)
            bytes_needed = MIN2(bytes_needed, info.component_size);
      }

      /* reduce constant offset */
      LoadEmitInfo new_info = info;
      new_info.ctx = ctx;
      Operand offset = info.offset;
      unsigned reduced_const_offset = const_offset;
      if (const_offset > params.max_const_offset) {
         uint32_t max_const_offset_plus_one = params.max_const_offset + 1;
         unsigned to_add = const_offset / max_const_offset_plus_one * max_const_offset_plus_one;
         reduced_const_offset %= max_const_offset_plus_one;

         /* For global loads with a 32-bit offset, the resource is the 64-bit address. */
         bool offset_changed = true;
         if (new_info.resource.id() && new_info.resource.size() == 2 &&
             add_might_overflow(ctx, info.offset_src, to_add)) {
            new_info.resource = add64_32(bld, new_info.resource, Operand::c32(to_add));
            new_info.resource_src = NULL;
            offset_changed = false;
         } else if (offset.isConstant()) {
            offset = Operand::c32(offset.constantValue() + to_add);
         } else if (offset.isUndefined()) {
            offset = Operand::c32(to_add);
         } else if (offset.regClass() == s1) {
            offset = bld.sop2(aco_opcode::s_add_i32, bld.def(s1), bld.def(s1, scc), offset,
                              Operand::c32(to_add));
         } else if (offset.regClass() == v1) {
            offset = bld.vadd32(bld.def(v1), offset, Operand::c32(to_add));
         } else {
            offset = Operand(add64_32(bld, offset.getTemp(), Operand::c32(to_add)));
         }
         if (offset_changed)
            new_info.offset_src = NULL;
      }
      new_info.offset = Operand(as_temp(bld, offset));
      new_info.const_offset = reduced_const_offset;

      unsigned align = align_offset ? 1 << (ffs(align_offset) - 1) : align_mul;

      Temp val = params.callback(bld, new_info, bytes_needed, align);

      /* the callback wrote directly to dst */
      if (val == info.dst) {
         assert(num_vals == 0);
         emit_split_vector(ctx, info.dst, info.num_components);
         return;
      }

      /* add result to list and advance */
      if (info.component_stride) {
         assert(val.bytes() % info.component_size == 0);
         unsigned num_loaded_components = val.bytes() / info.component_size;
         unsigned advance_bytes = info.component_stride * num_loaded_components;
         const_offset += advance_bytes;
         align_offset = (align_offset + advance_bytes) % align_mul;
      } else {
         const_offset += val.bytes();
         align_offset = (align_offset + val.bytes()) % align_mul;
      }
      bytes_read += val.bytes();
      vals[num_vals++] = val;
   }

   /* create array of components */
   unsigned components_split = 0;
   std::array<Temp, NIR_MAX_VEC_COMPONENTS> allocated_vec;
   bool has_vgprs = false;
   for (unsigned i = 0; i < num_vals;) {
      Temp* const tmp = (Temp*)alloca(num_vals * sizeof(Temp));
      unsigned num_tmps = 0;
      unsigned tmp_size = 0;
      RegType reg_type = RegType::sgpr;
      while ((!tmp_size || (tmp_size % component_size)) && i < num_vals) {
         if (vals[i].type() == RegType::vgpr)
            reg_type = RegType::vgpr;
         tmp_size += vals[i].bytes();
         tmp[num_tmps++] = vals[i++];
      }
      if (num_tmps > 1) {
         aco_ptr<Instruction> vec{
            create_instruction(aco_opcode::p_create_vector, Format::PSEUDO, num_tmps, 1)};
         for (unsigned j = 0; j < num_tmps; j++)
            vec->operands[j] = Operand(tmp[j]);
         tmp[0] = bld.tmp(RegClass::get(reg_type, tmp_size));
         vec->definitions[0] = Definition(tmp[0]);
         bld.insert(std::move(vec));
      }

      if (tmp[0].bytes() % component_size) {
         /* trim tmp[0] */
         assert(i == num_vals);
         RegClass new_rc =
            RegClass::get(reg_type, tmp[0].bytes() / component_size * component_size);
         tmp[0] =
            bld.pseudo(aco_opcode::p_extract_vector, bld.def(new_rc), tmp[0], Operand::zero());
      }

      RegClass elem_rc = RegClass::get(reg_type, component_size);

      unsigned start = components_split;

      if (tmp_size == elem_rc.bytes()) {
         allocated_vec[components_split++] = tmp[0];
      } else {
         assert(tmp_size % elem_rc.bytes() == 0);
         aco_ptr<Instruction> split{create_instruction(aco_opcode::p_split_vector, Format::PSEUDO,
                                                       1, tmp_size / elem_rc.bytes())};
         for (auto& def : split->definitions) {
            Temp component = bld.tmp(elem_rc);
            allocated_vec[components_split++] = component;
            def = Definition(component);
         }
         split->operands[0] = Operand(tmp[0]);
         bld.insert(std::move(split));
      }

      /* try to p_as_uniform early so we can create more optimizable code and
       * also update allocated_vec */
      for (unsigned j = start; j < components_split; j++) {
         if (allocated_vec[j].bytes() % 4 == 0 && info.dst.type() == RegType::sgpr)
            allocated_vec[j] = bld.as_uniform(allocated_vec[j]);
         has_vgprs |= allocated_vec[j].type() == RegType::vgpr;
      }
   }

   /* concatenate components and p_as_uniform() result if needed */
   if (info.dst.type() == RegType::vgpr || !has_vgprs)
      ctx->allocated_vec.emplace(info.dst.id(), allocated_vec);

   int padding_bytes =
      MAX2((int)info.dst.bytes() - int(allocated_vec[0].bytes() * info.num_components), 0);

   aco_ptr<Instruction> vec{create_instruction(aco_opcode::p_create_vector, Format::PSEUDO,
                                               info.num_components + !!padding_bytes, 1)};
   for (unsigned i = 0; i < info.num_components; i++)
      vec->operands[i] = Operand(allocated_vec[i]);
   if (padding_bytes)
      vec->operands[info.num_components] = Operand(RegClass::get(RegType::vgpr, padding_bytes));
   if (info.dst.type() == RegType::sgpr && has_vgprs) {
      Temp tmp = bld.tmp(RegType::vgpr, info.dst.size());
      vec->definitions[0] = Definition(tmp);
      bld.insert(std::move(vec));
      bld.pseudo(aco_opcode::p_as_uniform, Definition(info.dst), tmp);
   } else {
      vec->definitions[0] = Definition(info.dst);
      bld.insert(std::move(vec));
   }
}

std::pair<aco_opcode, unsigned>
get_smem_opcode(amd_gfx_level level, unsigned bytes, bool buffer, bool round_down)
{
   if (bytes <= 1 && level >= GFX12)
      return {buffer ? aco_opcode::s_buffer_load_ubyte : aco_opcode::s_load_ubyte, 1};
   else if (bytes <= (round_down ? 3 : 2) && level >= GFX12)
      return {buffer ? aco_opcode::s_buffer_load_ushort : aco_opcode::s_load_ushort, 2};
   else if (bytes <= (round_down ? 7 : 4))
      return {buffer ? aco_opcode::s_buffer_load_dword : aco_opcode::s_load_dword, 4};
   else if (bytes <= (round_down ? (level >= GFX12 ? 11 : 15) : 8))
      return {buffer ? aco_opcode::s_buffer_load_dwordx2 : aco_opcode::s_load_dwordx2, 8};
   else if (bytes <= (round_down ? 15 : 12) && level >= GFX12)
      return {buffer ? aco_opcode::s_buffer_load_dwordx3 : aco_opcode::s_load_dwordx3, 12};
   else if (bytes <= (round_down ? 31 : 16))
      return {buffer ? aco_opcode::s_buffer_load_dwordx4 : aco_opcode::s_load_dwordx4, 16};
   else if (bytes <= (round_down ? 63 : 32))
      return {buffer ? aco_opcode::s_buffer_load_dwordx8 : aco_opcode::s_load_dwordx8, 32};
   else
      return {buffer ? aco_opcode::s_buffer_load_dwordx16 : aco_opcode::s_load_dwordx16, 64};
}

unsigned
src_has_req_lsb(isel_context* ctx, nir_src* src, unsigned req)
{
   return src && nir_def_num_lsb_zero(ctx->numlsb_ht, nir_get_scalar(src->ssa, 0)) >= req;
}

Temp
smem_load_callback(Builder& bld, const LoadEmitInfo& info, unsigned bytes_needed, unsigned align)
{
   /* Only scalar sub-dword loads are supported. */
   assert(bytes_needed % 4 == 0 || bytes_needed <= 2);
   assert(align >= MIN2(bytes_needed, 4));

   bld.program->has_smem_buffer_or_global_loads = true;

   uint32_t const_offset = info.const_offset;
   Temp offset = info.offset.getTemp();
   bool buffer = info.resource.id() && info.resource.bytes() == 16;
   Temp addr = info.resource;
   if (!buffer && !addr.id()) {
      addr = offset;
      offset = Temp();
   }

   std::pair<aco_opcode, unsigned> smaller =
      get_smem_opcode(bld.program->gfx_level, bytes_needed, buffer, true);
   std::pair<aco_opcode, unsigned> larger =
      get_smem_opcode(bld.program->gfx_level, bytes_needed, buffer, false);

   /* Only round-up global loads if it's aligned so that it won't cross pages */
   aco_opcode op;
   std::tie(op, bytes_needed) =
      buffer || (align % util_next_power_of_two(larger.second) == 0) ? larger : smaller;

   /* Use a s4 regclass for dwordx3 loads. Even if the register allocator aligned s3 SMEM
    * definitions correctly, multiple dwordx3 loads can make very inefficient use of the register
    * file. There might be a single SGPR hole between each s3 temporary, making no space for a
    * vector without a copy for each SGPR needed. Using a s4 definition instead should help avoid
    * this situation by preventing the scheduler and register allocator from assuming that the 4th
    * SGPR of each definition in a sequence of dwordx3 SMEM loads is free for use by vector
    * temporaries.
    */
   RegClass rc(RegType::sgpr, DIV_ROUND_UP(util_next_power_of_two(bytes_needed), 4u));

   unsigned req_lsb_zero = bytes_needed == 1 ? 0 : (bytes_needed == 2 ? 1 : 2);
   if (!buffer) {
      /* We require each offset source and the final address to be aligned, so ensure at least
       * two sources are aligned. The remaining one can then be assumed to be aligned, otherwise the
       * final address is unaligned. */
      // TODO: lower in NIR
      bool addr_aligned = src_has_req_lsb(info.ctx, info.resource_src, req_lsb_zero);
      bool offset_aligned =
         !offset.id() || src_has_req_lsb(info.ctx, info.offset_src, req_lsb_zero);
      bool const_aligned = !const_offset || ffs(const_offset) > req_lsb_zero;

      if (!offset_aligned && (!addr_aligned || !const_aligned)) {
         addr = add64_32(bld, addr, Operand(offset));
         offset = Temp();
      }
      if (!const_aligned && (!addr_aligned || !offset_aligned)) {
         addr = add64_32(bld, addr, Operand::c32(const_offset));
         const_offset = 0;
      }
   } else {
      /* We assume the buffer resource is also aligned. */
      assert(!const_offset || ffs(const_offset) > req_lsb_zero);
   }

   bool soe = !buffer && offset.id() && const_offset && bld.program->gfx_level >= GFX9;
   aco_ptr<Instruction> load{create_instruction(op, Format::SMEM, 2 + soe, 1)};
   if (buffer) {
      if (const_offset)
         offset = bld.sop2(aco_opcode::s_add_u32, bld.def(s1), bld.def(s1, scc), offset,
                           Operand::c32(const_offset));
      load->operands[0] = Operand(info.resource);
      load->operands[1] = Operand(offset);
   } else {
      load->operands[0] = Operand(addr);
      if (soe) {
         load->operands[1] = Operand::c32(const_offset);
         load->operands[2] = Operand(offset);
      } else if (offset.id() && const_offset) {
         load->operands[0] = Operand(add64_32(bld, addr, Operand::c32(const_offset)));
         load->operands[1] = Operand(offset);
      } else if (offset.id()) {
         load->operands[1] = Operand(offset);
      } else {
         load->operands[1] = Operand::c32(const_offset);
      }
   }
   Temp val = info.dst.regClass() == rc && rc.bytes() == bytes_needed ? info.dst : bld.tmp(rc);
   load->definitions[0] = Definition(val);
   load->smem().cache = info.cache;
   load->smem().sync = info.sync;
   bld.insert(std::move(load));

   if (rc.bytes() > bytes_needed) {
      rc = RegClass(RegType::sgpr, DIV_ROUND_UP(bytes_needed, 4u));
      Temp val2 = info.dst.regClass() == rc ? info.dst : bld.tmp(rc);
      val = bld.pseudo(aco_opcode::p_extract_vector, Definition(val2), val, Operand::c32(0u));
   }

   return val;
}

const EmitLoadParameters smem_load_params{smem_load_callback, 1023};

Temp
mubuf_load_callback(Builder& bld, const LoadEmitInfo& info, unsigned bytes_needed, unsigned align_)
{
   Temp offset = info.offset.getTemp();
   Operand vaddr = offset.type() == RegType::vgpr ? Operand(offset) : Operand(v1);
   Operand soffset = offset.type() == RegType::sgpr ? Operand(offset) : Operand::c32(0);

   if (info.soffset.id()) {
      if (soffset.isTemp())
         vaddr = bld.copy(bld.def(v1), soffset);
      soffset = Operand(info.soffset);
   }

   if (soffset.isUndefined())
      soffset = Operand::zero();

   bool offen = !vaddr.isUndefined();
   bool idxen = info.idx.id();

   if (offen && idxen)
      vaddr = bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), info.idx, vaddr);
   else if (idxen)
      vaddr = Operand(info.idx);

   bool atomic64 =
      (info.sync.semantics & semantic_atomic) && info.component_size == 8 && align_ >= 8;
   bool use_mtbuf = false;

   unsigned bytes_size = 0;
   aco_opcode op;
   if (bytes_needed == 1 || align_ % 2) {
      bytes_size = 1;
      op = bld.program->gfx_level >= GFX9 ? aco_opcode::buffer_load_ubyte_d16
                                          : aco_opcode::buffer_load_ubyte;
   } else if (bytes_needed == 2 || align_ % 4) {
      bytes_size = 2;
      op = bld.program->gfx_level >= GFX9 ? aco_opcode::buffer_load_short_d16
                                          : aco_opcode::buffer_load_ushort;
   } else if (bytes_needed <= 4) {
      bytes_size = 4;
      op = aco_opcode::buffer_load_dword;
   } else if (bytes_needed <= 8 || atomic64) {
      bytes_size = 8;
      /* Use MTBUF for 64-bit atomic loads for correct bounds checking. */
      use_mtbuf = atomic64;
      op = use_mtbuf ? aco_opcode::tbuffer_load_format_xy : aco_opcode::buffer_load_dwordx2;
   } else if (bytes_needed <= 12 && bld.program->gfx_level > GFX6) {
      bytes_size = 12;
      op = aco_opcode::buffer_load_dwordx3;
   } else {
      bytes_size = 16;
      op = aco_opcode::buffer_load_dwordx4;
   }
   aco_ptr<Instruction> mubuf{
      create_instruction(op, use_mtbuf ? Format::MTBUF : Format::MUBUF, 3 + 2 * info.disable_wqm, 1)};
   mubuf->operands[0] = Operand(info.resource);
   mubuf->operands[1] = vaddr;
   mubuf->operands[2] = soffset;
   if (use_mtbuf) {
      mubuf->mtbuf().offen = offen;
      mubuf->mtbuf().idxen = idxen;
      mubuf->mtbuf().cache = info.cache;
      mubuf->mtbuf().sync = info.sync;
      mubuf->mtbuf().offset = info.const_offset;
      mubuf->mtbuf().dfmt = V_008F0C_BUF_DATA_FORMAT_32_32;
      mubuf->mtbuf().nfmt = V_008F0C_BUF_NUM_FORMAT_UINT;
      init_disable_wqm(bld, mubuf->mtbuf(), info.disable_wqm);
   } else {
      mubuf->mubuf().offen = offen;
      mubuf->mubuf().idxen = idxen;
      mubuf->mubuf().cache = info.cache;
      mubuf->mubuf().sync = info.sync;
      mubuf->mubuf().offset = info.const_offset;
      init_disable_wqm(bld, mubuf->mubuf(), info.disable_wqm);
   }
   RegClass rc = RegClass::get(RegType::vgpr, bytes_size);
   Temp val = rc == info.dst.regClass() ? info.dst : bld.tmp(rc);
   mubuf->definitions[0] = Definition(val);
   bld.insert(std::move(mubuf));

   return val;
}

const EmitLoadParameters mubuf_load_params{mubuf_load_callback, 4095};

Temp
mubuf_load_format_callback(Builder& bld, const LoadEmitInfo& info, unsigned bytes_needed,
                           unsigned align_)
{
   Temp offset = info.offset.getTemp();
   Operand vaddr = offset.type() == RegType::vgpr ? Operand(offset) : Operand(v1);
   Operand soffset = offset.type() == RegType::sgpr ? Operand(offset) : Operand::c32(0);

   if (info.soffset.id()) {
      if (soffset.isTemp())
         vaddr = bld.copy(bld.def(v1), soffset);
      soffset = Operand(info.soffset);
   }

   if (soffset.isUndefined())
      soffset = Operand::zero();

   bool offen = !vaddr.isUndefined();
   bool idxen = info.idx.id();

   if (offen && idxen)
      vaddr = bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), info.idx, vaddr);
   else if (idxen)
      vaddr = Operand(info.idx);

   aco_opcode op = aco_opcode::num_opcodes;
   if (info.component_size == 2) {
      switch (bytes_needed) {
      case 2: op = aco_opcode::buffer_load_format_d16_x; break;
      case 4: op = aco_opcode::buffer_load_format_d16_xy; break;
      case 6: op = aco_opcode::buffer_load_format_d16_xyz; break;
      case 8: op = aco_opcode::buffer_load_format_d16_xyzw; break;
      default: UNREACHABLE("invalid buffer load format size"); break;
      }
   } else {
      assert(info.component_size == 4);
      switch (bytes_needed) {
      case 4: op = aco_opcode::buffer_load_format_x; break;
      case 8: op = aco_opcode::buffer_load_format_xy; break;
      case 12: op = aco_opcode::buffer_load_format_xyz; break;
      case 16: op = aco_opcode::buffer_load_format_xyzw; break;
      default: UNREACHABLE("invalid buffer load format size"); break;
      }
   }

   aco_ptr<Instruction> mubuf{create_instruction(op, Format::MUBUF, 3, 1)};
   mubuf->operands[0] = Operand(info.resource);
   mubuf->operands[1] = vaddr;
   mubuf->operands[2] = soffset;
   mubuf->mubuf().offen = offen;
   mubuf->mubuf().idxen = idxen;
   mubuf->mubuf().cache = info.cache;
   mubuf->mubuf().sync = info.sync;
   mubuf->mubuf().offset = info.const_offset;
   RegClass rc = RegClass::get(RegType::vgpr, bytes_needed);
   Temp val = rc == info.dst.regClass() ? info.dst : bld.tmp(rc);
   mubuf->definitions[0] = Definition(val);
   bld.insert(std::move(mubuf));

   return val;
}

const EmitLoadParameters mubuf_load_format_params{mubuf_load_format_callback, 4095};

Temp
scratch_load_callback(Builder& bld, const LoadEmitInfo& info, unsigned bytes_needed,
                      unsigned align_)
{
   unsigned bytes_size = 0;
   aco_opcode op;
   if (bytes_needed == 1 || align_ % 2u) {
      bytes_size = 1;
      op = aco_opcode::scratch_load_ubyte_d16;
   } else if (bytes_needed == 2 || align_ % 4u) {
      bytes_size = 2;
      op = aco_opcode::scratch_load_short_d16;
   } else if (bytes_needed <= 4) {
      bytes_size = 4;
      op = aco_opcode::scratch_load_dword;
   } else if (bytes_needed <= 8) {
      bytes_size = 8;
      op = aco_opcode::scratch_load_dwordx2;
   } else if (bytes_needed <= 12) {
      bytes_size = 12;
      op = aco_opcode::scratch_load_dwordx3;
   } else {
      bytes_size = 16;
      op = aco_opcode::scratch_load_dwordx4;
   }
   RegClass rc = RegClass::get(RegType::vgpr, bytes_size);
   Temp val = rc == info.dst.regClass() ? info.dst : bld.tmp(rc);
   aco_ptr<Instruction> flat{create_instruction(op, Format::SCRATCH, 2 + 2 * info.disable_wqm, 1)};
   Temp offset = info.offset.getTemp();
   flat->operands[0] = offset.regClass() == s1 ? Operand(v1) : Operand(offset);
   flat->operands[1] = offset.regClass() == s1 ? Operand(offset) : Operand(s1);
   flat->scratch().sync = info.sync;
   flat->scratch().offset = info.const_offset;
   flat->definitions[0] = Definition(val);
   init_disable_wqm(bld, flat->scratch(), info.disable_wqm);
   bld.insert(std::move(flat));

   return val;
}

const EmitLoadParameters scratch_mubuf_load_params{mubuf_load_callback, 4095};
const EmitLoadParameters scratch_flat_load_params{scratch_load_callback, 2047};

Temp
get_mubuf_global_rsrc(Builder& bld, Temp addr)
{
   uint32_t desc[4];
   ac_build_raw_buffer_descriptor(bld.program->gfx_level, 0, 0xffffffff, desc);

   if (addr.type() == RegType::vgpr)
      return bld.pseudo(aco_opcode::p_create_vector, bld.def(s4), Operand::zero(), Operand::zero(),
                        Operand::c32(desc[2]), Operand::c32(desc[3]));
   return bld.pseudo(aco_opcode::p_create_vector, bld.def(s4), addr, Operand::c32(desc[2]),
                     Operand::c32(desc[3]));
}

Temp
add64_const64(Builder& bld, Temp addr, uint64_t offset)
{
   /* This could be more efficient if offset>UINT32_MAX by doing a full 64-bit addition,
    * but that should be really rare.
    */
   while (offset) {
      uint32_t src2 = MIN2(offset, UINT32_MAX);
      addr = add64_32(bld, addr, Operand::c32(src2));
      offset -= src2;
   }
   return addr;
}

Format
lower_global_address(isel_context* ctx, Builder& bld, uint32_t offset_in, Temp* address_inout,
                     uint32_t* const_offset_inout, Temp* offset_inout, nir_src* offset_src)
{
   Temp address = *address_inout;
   uint64_t const_offset = *const_offset_inout + offset_in;
   Temp offset = *offset_inout;

   Format format = Format::MUBUF;
   if (bld.program->gfx_level >= GFX9)
      format = Format::GLOBAL;
   else if (bld.program->gfx_level >= GFX7 && address.type() == RegType::vgpr)
      format = Format::FLAT;

   uint64_t max_const_offset_plus_one =
      1; /* GFX7/8/9: FLAT loads do not support constant offsets */
   if (format == Format::GLOBAL)
      max_const_offset_plus_one = bld.program->dev.scratch_global_offset_max + UINT64_C(1);
   else if (format == Format::MUBUF)
      max_const_offset_plus_one = bld.program->dev.buf_offset_max + 1;
   uint64_t excess_offset = const_offset - (const_offset % max_const_offset_plus_one);
   const_offset %= max_const_offset_plus_one;

   if (!offset.id()) {
      address = add64_const64(bld, address, excess_offset / UINT32_MAX * UINT32_MAX);
      if (excess_offset % UINT32_MAX)
         offset = bld.copy(bld.def(s1), Operand::c32(excess_offset % UINT32_MAX));
   } else {
      /* If we add to "offset", we would transform the indended
       * "address + u2u64(offset) + u2u64(const_offset)" into
       * "address + u2u64(offset + const_offset)", so add to the address.
       */
      address = add64_const64(bld, address, excess_offset);
   }

   if (format == Format::MUBUF) {
      /* GFX6 (MUBUF): (SGPR address, SGPR offset) or (SGPR address, VGPR offset) */
      /* GFX6 (MUBUF-addr64): (VGPR address, SGPR offset) */
      /* Disallow SGPR address with both a const_offset and offset in case of possible overflow. */
      if (offset.id() &&
          (address.type() == RegType::vgpr ? offset.type() != RegType::sgpr
                                           : add_might_overflow(ctx, offset_src, const_offset))) {
         if (offset.type() == RegType::vgpr && bld.program->gfx_level > GFX6) {
            assert(address.type() == RegType::sgpr);
            address = add64_const64(bld, address, const_offset);
            const_offset = 0;
         } else {
            address = add64_32(bld, address, Operand(offset));
            offset = Temp();
         }
      }
      offset = offset.id() ? offset : bld.copy(bld.def(s1), Operand::zero());
   } else if (format == Format::FLAT) {
      /* GFX7,8 (FLAT): VGPR address */
      if (offset.id()) {
         address = add64_32(bld, address, Operand(offset));
         offset = Temp();
      }
      address = as_vgpr(bld, address);
   } else {
      /* GFX9+ (GLOBAL): (VGPR address), or (SGPR address and VGPR offset) */
      if (address.type() == RegType::vgpr && offset.id()) {
         address = add64_32(bld, address, Operand(offset));
         offset = Temp();
      } else if (address.type() == RegType::sgpr && offset.id()) {
         offset = as_vgpr(bld, offset);
      }
      if (address.type() == RegType::sgpr && !offset.id())
         offset = bld.copy(bld.def(v1), bld.copy(bld.def(s1), Operand::zero()));
   }

   *address_inout = address;
   *const_offset_inout = const_offset;
   *offset_inout = offset;

   return format;
}

Temp
global_load_callback(Builder& bld, const LoadEmitInfo& info, unsigned bytes_needed, unsigned align_)
{
   Temp offset = info.offset.getTemp();
   Temp addr = info.resource;
   if (!addr.id()) {
      addr = offset;
      offset = Temp();
   }
   uint32_t const_offset = info.const_offset;
   Format format =
      lower_global_address(info.ctx, bld, 0, &addr, &const_offset, &offset, info.offset_src);

   unsigned bytes_size = 0;
   bool use_mubuf = format == Format::MUBUF;
   bool global = format == Format::GLOBAL;
   aco_opcode op;
   if (bytes_needed == 1 || align_ % 2u) {
      bytes_size = 1;
      op = use_mubuf ? aco_opcode::buffer_load_ubyte
           : global  ? aco_opcode::global_load_ubyte_d16
                     : aco_opcode::flat_load_ubyte;
   } else if (bytes_needed == 2 || align_ % 4u) {
      bytes_size = 2;
      op = use_mubuf ? aco_opcode::buffer_load_ushort
           : global  ? aco_opcode::global_load_short_d16
                     : aco_opcode::flat_load_ushort;
   } else if (bytes_needed <= 4) {
      bytes_size = 4;
      op = use_mubuf ? aco_opcode::buffer_load_dword
           : global  ? aco_opcode::global_load_dword
                     : aco_opcode::flat_load_dword;
   } else if (bytes_needed <= 8 || (bytes_needed <= 12 && bld.program->gfx_level == GFX6)) {
      bytes_size = 8;
      op = use_mubuf ? aco_opcode::buffer_load_dwordx2
           : global  ? aco_opcode::global_load_dwordx2
                     : aco_opcode::flat_load_dwordx2;
   } else if (bytes_needed <= 12) {
      bytes_size = 12;
      op = use_mubuf ? aco_opcode::buffer_load_dwordx3
           : global  ? aco_opcode::global_load_dwordx3
                     : aco_opcode::flat_load_dwordx3;
   } else {
      bytes_size = 16;
      op = use_mubuf ? aco_opcode::buffer_load_dwordx4
           : global  ? aco_opcode::global_load_dwordx4
                     : aco_opcode::flat_load_dwordx4;
   }
   RegClass rc = RegClass::get(RegType::vgpr, bytes_size);
   Temp val = rc == info.dst.regClass() ? info.dst : bld.tmp(rc);
   if (use_mubuf) {
      assert(bld.program->gfx_level == GFX6 || addr.type() != RegType::vgpr);

      aco_ptr<Instruction> mubuf{
         create_instruction(op, Format::MUBUF, 3 + 2 * info.disable_wqm, 1)};
      mubuf->operands[0] = Operand(get_mubuf_global_rsrc(bld, addr));
      if (addr.type() == RegType::vgpr)
         mubuf->operands[1] = Operand(addr);
      else if (offset.type() == RegType::vgpr)
         mubuf->operands[1] = Operand(offset);
      else
         mubuf->operands[1] = Operand(v1);
      mubuf->operands[2] = offset.type() == RegType::sgpr ? Operand(offset) : Operand::c32(0);
      mubuf->mubuf().offen = offset.type() == RegType::vgpr;
      mubuf->mubuf().cache = info.cache;
      mubuf->mubuf().offset = const_offset;
      mubuf->mubuf().addr64 = addr.type() == RegType::vgpr;
      mubuf->mubuf().sync = info.sync;
      mubuf->definitions[0] = Definition(val);
      init_disable_wqm(bld, mubuf->mubuf(), info.disable_wqm);
      bld.insert(std::move(mubuf));
   } else {
      aco_ptr<Instruction> flat{create_instruction(op, format, 2 + 2 * info.disable_wqm, 1)};
      if (addr.regClass() == s2) {
         assert(global && offset.id() && offset.type() == RegType::vgpr);
         flat->operands[0] = Operand(offset);
         flat->operands[1] = Operand(addr);
      } else {
         assert(addr.type() == RegType::vgpr && !offset.id());
         flat->operands[0] = Operand(addr);
         flat->operands[1] = Operand(s1);
      }
      flat->flatlike().cache = info.cache;
      flat->flatlike().sync = info.sync;
      assert(global || !const_offset);
      flat->flatlike().offset = const_offset;
      flat->definitions[0] = Definition(val);
      init_disable_wqm(bld, flat->flatlike(), info.disable_wqm);
      bld.insert(std::move(flat));
   }

   return val;
}

const EmitLoadParameters global_load_params{global_load_callback, UINT32_MAX};

void
split_store_data(isel_context* ctx, RegType dst_type, unsigned count, Temp* dst, unsigned* bytes,
                 Temp src)
{
   if (!count)
      return;

   Builder bld(ctx->program, ctx->block);

   /* count == 1 fast path */
   if (count == 1) {
      if (dst_type == RegType::sgpr)
         dst[0] = bld.as_uniform(src);
      else
         dst[0] = as_vgpr(ctx, src);
      return;
   }

   /* elem_size_bytes is the greatest common divisor which is a power of 2 */
   unsigned elem_size_bytes =
      1u << (ffs(std::accumulate(bytes, bytes + count, 8, std::bit_or<>{})) - 1);

   ASSERTED bool is_subdword = elem_size_bytes < 4;
   assert(!is_subdword || dst_type == RegType::vgpr);

   for (unsigned i = 0; i < count; i++)
      dst[i] = bld.tmp(RegClass::get(dst_type, bytes[i]));

   std::vector<Temp> temps;
   /* use allocated_vec if possible */
   auto it = ctx->allocated_vec.find(src.id());
   if (it != ctx->allocated_vec.end()) {
      if (!it->second[0].id())
         goto split;
      unsigned elem_size = it->second[0].bytes();
      assert(src.bytes() % elem_size == 0);

      for (unsigned i = 0; i < src.bytes() / elem_size; i++) {
         if (!it->second[i].id())
            goto split;
      }
      if (elem_size_bytes % elem_size)
         goto split;

      temps.insert(temps.end(), it->second.begin(), it->second.begin() + src.bytes() / elem_size);
      elem_size_bytes = elem_size;
   }

split:
   /* split src if necessary */
   if (temps.empty()) {
      if (is_subdword && src.type() == RegType::sgpr)
         src = as_vgpr(ctx, src);
      if (dst_type == RegType::sgpr)
         src = bld.as_uniform(src);

      unsigned num_elems = src.bytes() / elem_size_bytes;
      aco_ptr<Instruction> split{
         create_instruction(aco_opcode::p_split_vector, Format::PSEUDO, 1, num_elems)};
      split->operands[0] = Operand(src);
      for (unsigned i = 0; i < num_elems; i++) {
         temps.emplace_back(bld.tmp(RegClass::get(dst_type, elem_size_bytes)));
         split->definitions[i] = Definition(temps.back());
      }
      bld.insert(std::move(split));
   }

   unsigned idx = 0;
   for (unsigned i = 0; i < count; i++) {
      unsigned op_count = dst[i].bytes() / elem_size_bytes;
      if (op_count == 1) {
         if (dst_type == RegType::sgpr)
            dst[i] = bld.as_uniform(temps[idx++]);
         else
            dst[i] = as_vgpr(ctx, temps[idx++]);
         continue;
      }

      aco_ptr<Instruction> vec{
         create_instruction(aco_opcode::p_create_vector, Format::PSEUDO, op_count, 1)};
      for (unsigned j = 0; j < op_count; j++) {
         Temp tmp = temps[idx++];
         if (dst_type == RegType::sgpr)
            tmp = bld.as_uniform(tmp);
         vec->operands[j] = Operand(tmp);
      }
      vec->definitions[0] = Definition(dst[i]);
      bld.insert(std::move(vec));
   }
   return;
}

bool
scan_write_mask(uint32_t mask, uint32_t todo_mask, int* start, int* count)
{
   unsigned start_elem = ffs(todo_mask) - 1;
   bool skip = !(mask & (1 << start_elem));
   if (skip)
      mask = ~mask & todo_mask;

   mask &= todo_mask;

   u_bit_scan_consecutive_range(&mask, start, count);

   return !skip;
}

void
advance_write_mask(uint32_t* todo_mask, int start, int count)
{
   *todo_mask &= ~u_bit_consecutive(0, count) << start;
}

aco_opcode
get_buffer_store_op(unsigned bytes)
{
   switch (bytes) {
   case 1: return aco_opcode::buffer_store_byte;
   case 2: return aco_opcode::buffer_store_short;
   case 4: return aco_opcode::buffer_store_dword;
   case 8: return aco_opcode::buffer_store_dwordx2;
   case 12: return aco_opcode::buffer_store_dwordx3;
   case 16: return aco_opcode::buffer_store_dwordx4;
   }
   UNREACHABLE("Unexpected store size");
   return aco_opcode::num_opcodes;
}

void
split_buffer_store(isel_context* ctx, nir_intrinsic_instr* instr, bool smem, RegType dst_type,
                   Temp data, unsigned writemask, int swizzle_element_size, unsigned* write_count,
                   Temp* write_datas, unsigned* offsets)
{
   unsigned write_count_with_skips = 0;
   bool skips[16];
   unsigned bytes[16];

   /* determine how to split the data */
   unsigned todo = u_bit_consecutive(0, data.bytes());
   while (todo) {
      int offset, byte;
      skips[write_count_with_skips] = !scan_write_mask(writemask, todo, &offset, &byte);
      offsets[write_count_with_skips] = offset;
      if (skips[write_count_with_skips]) {
         bytes[write_count_with_skips] = byte;
         advance_write_mask(&todo, offset, byte);
         write_count_with_skips++;
         continue;
      }

      /* only supported sizes are 1, 2, 4, 8, 12 and 16 bytes and can't be
       * larger than swizzle_element_size */
      byte = MIN2(byte, swizzle_element_size);
      if (byte % 4)
         byte = byte > 4 ? byte & ~0x3 : MIN2(byte, 2);

      /* SMEM and GFX6 VMEM can't emit 12-byte stores */
      if ((ctx->program->gfx_level == GFX6 || smem) && byte == 12)
         byte = 8;

      /* dword or larger stores have to be dword-aligned */
      unsigned align_mul = nir_intrinsic_align_mul(instr);
      unsigned align_offset = nir_intrinsic_align_offset(instr) + offset;
      bool dword_aligned = align_offset % 4 == 0 && align_mul % 4 == 0;
      if (!dword_aligned)
         byte = MIN2(byte, (align_offset % 2 == 0 && align_mul % 2 == 0) ? 2 : 1);

      bytes[write_count_with_skips] = byte;
      advance_write_mask(&todo, offset, byte);
      write_count_with_skips++;
   }

   /* actually split data */
   split_store_data(ctx, dst_type, write_count_with_skips, write_datas, bytes, data);

   /* remove skips */
   for (unsigned i = 0; i < write_count_with_skips; i++) {
      if (skips[i])
         continue;
      write_datas[*write_count] = write_datas[i];
      offsets[*write_count] = offsets[i];
      (*write_count)++;
   }
}

inline unsigned
resolve_excess_vmem_const_offset(Builder& bld, Temp& voffset, unsigned const_offset)
{
   uint32_t limit = bld.program->dev.buf_offset_max + 1;
   if (const_offset >= limit) {
      unsigned excess_const_offset = const_offset / limit * limit;
      const_offset %= limit;

      if (!voffset.id())
         voffset = bld.copy(bld.def(v1), Operand::c32(excess_const_offset));
      else if (unlikely(voffset.regClass() == s1))
         voffset = bld.sop2(aco_opcode::s_add_u32, bld.def(s1), bld.def(s1, scc),
                            Operand::c32(excess_const_offset), Operand(voffset));
      else if (likely(voffset.regClass() == v1))
         voffset = bld.vadd32(bld.def(v1), Operand(voffset), Operand::c32(excess_const_offset));
      else
         UNREACHABLE("Unsupported register class of voffset");
   }

   return const_offset;
}

bool
store_output_to_temps(isel_context* ctx, nir_intrinsic_instr* instr)
{
   unsigned write_mask = nir_intrinsic_write_mask(instr);
   unsigned component = nir_intrinsic_component(instr);
   nir_src offset = *nir_get_io_offset_src(instr);

   if (!nir_src_is_const(offset) || nir_src_as_uint(offset))
      return false;

   Temp src = get_ssa_temp(ctx, instr->src[0].ssa);

   if (instr->src[0].ssa->bit_size == 64)
      write_mask = util_widen_mask(write_mask, 2);

   RegClass rc = instr->src[0].ssa->bit_size == 16 ? v2b : v1;

   /* Use semantic location as index. radv already uses it as intrinsic base
    * but radeonsi does not. We need to make LS output and TCS input index
    * match each other, so need to use semantic location explicitly. Also for
    * TCS epilog to index tess factor temps using semantic location directly.
    */
   nir_io_semantics sem = nir_intrinsic_io_semantics(instr);
   unsigned base = sem.location;
   if (ctx->stage == fragment_fs) {
      /* color result is a legacy slot which won't appear with data result
       * at the same time. Here we just use the data slot for it to simplify
       * code handling for both of them.
       */
      if (base == FRAG_RESULT_COLOR)
         base = FRAG_RESULT_DATA0;

      /* Sencond output of dual source blend just use data1 slot for simplicity,
       * because dual source blend does not support multi render target.
       */
      base += sem.dual_source_blend_index;
   }
   unsigned idx = base * 4u + component;

   for (unsigned i = 0; i < 8; ++i) {
      if (write_mask & (1 << i)) {
         ctx->outputs.mask[idx / 4u] |= 1 << (idx % 4u);
         ctx->outputs.temps[idx] = emit_extract_vector(ctx, src, i, rc);
      }
      idx++;
   }

   if (ctx->stage == fragment_fs && ctx->program->info.ps.has_epilog && base >= FRAG_RESULT_DATA0) {
      unsigned index = base - FRAG_RESULT_DATA0;

      if (nir_intrinsic_src_type(instr) == nir_type_float16) {
         ctx->output_color_types |= ACO_TYPE_FLOAT16 << (index * 2);
      } else if (nir_intrinsic_src_type(instr) == nir_type_int16) {
         ctx->output_color_types |= ACO_TYPE_INT16 << (index * 2);
      } else if (nir_intrinsic_src_type(instr) == nir_type_uint16) {
         ctx->output_color_types |= ACO_TYPE_UINT16 << (index * 2);
      }
   }

   return true;
}

bool
load_input_from_temps(isel_context* ctx, nir_intrinsic_instr* instr, Temp dst)
{
   /* Only TCS per-vertex inputs are supported by this function.
    * Per-vertex inputs only match between the VS/TCS invocation id when the number of invocations
    * is the same.
    */
   if (ctx->shader->info.stage != MESA_SHADER_TESS_CTRL || !ctx->tcs_in_out_eq)
      return false;

   /* This can only be indexing with invocation_id because all other access has been lowered
    * to load_shared.
    */
   nir_src* off_src = nir_get_io_offset_src(instr);
   assert(nir_src_is_const(*off_src));

   nir_io_semantics sem = nir_intrinsic_io_semantics(instr);

   unsigned idx =
      sem.location * 4u + nir_intrinsic_component(instr) + 4 * nir_src_as_uint(*off_src);
   Temp* src = &ctx->inputs.temps[idx];
   create_vec_from_array(ctx, src, dst.size(), dst.regClass().type(), 4u, 0, dst);

   return true;
}

void
visit_store_output(isel_context* ctx, nir_intrinsic_instr* instr)
{
   /* LS pass output to TCS by temp if they have same in/out patch size. */
   bool ls_need_output = ctx->stage == vertex_tess_control_hs &&
                         ctx->shader->info.stage == MESA_SHADER_VERTEX && ctx->tcs_in_out_eq;

   bool ps_need_output = ctx->stage == fragment_fs;

   if (ls_need_output || ps_need_output) {
      bool stored_to_temps = store_output_to_temps(ctx, instr);
      if (!stored_to_temps) {
         isel_err(nir_def_instr(instr->src[1].ssa),
                  "Unimplemented output offset instruction");
         abort();
      }
   } else {
      UNREACHABLE("Shader stage not implemented");
   }
}

void
visit_load_interpolated_input(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Temp dst = get_ssa_temp(ctx, &instr->def);
   Temp coords = get_ssa_temp(ctx, instr->src[0].ssa);
   unsigned idx = nir_intrinsic_base(instr);
   unsigned component = nir_intrinsic_component(instr);
   bool high_16bits = nir_intrinsic_io_semantics(instr).high_16bits;
   Temp prim_mask = get_arg(ctx, ctx->args->prim_mask);

   assert(nir_src_is_const(instr->src[1]) && !nir_src_as_uint(instr->src[1]));

   if (instr->def.num_components == 1) {
      emit_interp_instr(ctx, idx, component, coords, dst, prim_mask, high_16bits);
   } else {
      aco_ptr<Instruction> vec(create_instruction(aco_opcode::p_create_vector, Format::PSEUDO,
                                                  instr->def.num_components, 1));
      for (unsigned i = 0; i < instr->def.num_components; i++) {
         Temp tmp = ctx->program->allocateTmp(instr->def.bit_size == 16 ? v2b : v1);
         emit_interp_instr(ctx, idx, component + i, coords, tmp, prim_mask, high_16bits);
         vec->operands[i] = Operand(tmp);
      }
      vec->definitions[0] = Definition(dst);
      ctx->block->instructions.emplace_back(std::move(vec));
   }
}

Temp
mtbuf_load_callback(Builder& bld, const LoadEmitInfo& info, unsigned bytes_needed,
                    unsigned alignment)
{
   Temp offset = info.offset.getTemp();
   Operand vaddr = offset.type() == RegType::vgpr ? Operand(offset) : Operand(v1);
   Operand soffset = offset.type() == RegType::sgpr ? Operand(offset) : Operand::c32(0);

   if (info.soffset.id()) {
      if (soffset.isTemp())
         vaddr = bld.copy(bld.def(v1), soffset);
      soffset = Operand(info.soffset);
   }

   if (soffset.isUndefined())
      soffset = Operand::zero();

   const bool offen = !vaddr.isUndefined();
   const bool idxen = info.idx.id();

   if (offen && idxen)
      vaddr = bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), info.idx, vaddr);
   else if (idxen)
      vaddr = Operand(info.idx);

   /* Determine number of fetched components.
    * Note, ACO IR works with GFX6-8 nfmt + dfmt fields, these are later converted for GFX10+.
    */
   const struct ac_vtx_format_info* vtx_info =
      ac_get_vtx_format_info(GFX8, CHIP_POLARIS10, info.format);
   /* The number of channels in the format determines the memory range. */
   const unsigned max_components = vtx_info->num_channels;
   /* Calculate maximum number of components loaded according to alignment. */
   unsigned max_fetched_components = bytes_needed / info.component_size;
   max_fetched_components =
      ac_get_safe_fetch_size(bld.program->gfx_level, vtx_info, info.const_offset, max_components,
                             alignment, max_fetched_components);
   const unsigned fetch_fmt = vtx_info->hw_format[max_fetched_components - 1];
   /* Adjust bytes needed in case we need to do a smaller load due to alignment.
    * If a larger format is selected, it's still OK to load a smaller amount from it.
    */
   bytes_needed = MIN2(bytes_needed, max_fetched_components * info.component_size);
   unsigned bytes_size = 0;
   const unsigned bit_size = info.component_size * 8;
   aco_opcode op = aco_opcode::num_opcodes;

   if (bytes_needed == 2) {
      bytes_size = 2;
      op = aco_opcode::tbuffer_load_format_d16_x;
   } else if (bytes_needed <= 4) {
      bytes_size = 4;
      if (bit_size == 16)
         op = aco_opcode::tbuffer_load_format_d16_xy;
      else
         op = aco_opcode::tbuffer_load_format_x;
   } else if (bytes_needed <= 6) {
      bytes_size = 6;
      if (bit_size == 16)
         op = aco_opcode::tbuffer_load_format_d16_xyz;
      else
         op = aco_opcode::tbuffer_load_format_xy;
   } else if (bytes_needed <= 8) {
      bytes_size = 8;
      if (bit_size == 16)
         op = aco_opcode::tbuffer_load_format_d16_xyzw;
      else
         op = aco_opcode::tbuffer_load_format_xy;
   } else if (bytes_needed <= 12) {
      bytes_size = 12;
      op = aco_opcode::tbuffer_load_format_xyz;
   } else {
      bytes_size = 16;
      op = aco_opcode::tbuffer_load_format_xyzw;
   }

   /* Abort when suitable opcode wasn't found so we don't compile buggy shaders. */
   if (op == aco_opcode::num_opcodes) {
      aco_err(bld.program, "unsupported bit size for typed buffer load");
      abort();
   }

   aco_ptr<Instruction> mtbuf{create_instruction(op, Format::MTBUF, 3, 1)};
   mtbuf->operands[0] = Operand(info.resource);
   mtbuf->operands[1] = vaddr;
   mtbuf->operands[2] = soffset;
   mtbuf->mtbuf().offen = offen;
   mtbuf->mtbuf().idxen = idxen;
   mtbuf->mtbuf().cache = info.cache;
   mtbuf->mtbuf().sync = info.sync;
   mtbuf->mtbuf().offset = info.const_offset;
   mtbuf->mtbuf().dfmt = fetch_fmt & 0xf;
   mtbuf->mtbuf().nfmt = fetch_fmt >> 4;
   RegClass rc = RegClass::get(RegType::vgpr, bytes_size);
   Temp val = rc == info.dst.regClass() ? info.dst : bld.tmp(rc);
   mtbuf->definitions[0] = Definition(val);
   bld.insert(std::move(mtbuf));

   return val;
}

const EmitLoadParameters mtbuf_load_params{mtbuf_load_callback, 4095};

void
visit_load_fs_input(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   Temp dst = get_ssa_temp(ctx, &instr->def);
   nir_src offset = *nir_get_io_offset_src(instr);

   if (!nir_src_is_const(offset) || nir_src_as_uint(offset))
      isel_err(nir_def_instr(offset.ssa),
               "Unimplemented non-zero nir_intrinsic_load_input offset");

   Temp prim_mask = get_arg(ctx, ctx->args->prim_mask);

   unsigned idx = nir_intrinsic_base(instr);
   unsigned component = nir_intrinsic_component(instr);
   bool high_16bits = nir_intrinsic_io_semantics(instr).high_16bits;
   unsigned vertex_id = 0; /* P0 */

   if (instr->intrinsic == nir_intrinsic_load_input_vertex)
      vertex_id = nir_src_as_uint(instr->src[0]);

   if (instr->def.num_components == 1 && instr->def.bit_size != 64) {
      emit_interp_mov_instr(ctx, idx, component, vertex_id, dst, prim_mask, high_16bits);
   } else {
      unsigned num_components = instr->def.num_components;
      if (instr->def.bit_size == 64)
         num_components *= 2;
      aco_ptr<Instruction> vec{
         create_instruction(aco_opcode::p_create_vector, Format::PSEUDO, num_components, 1)};
      for (unsigned i = 0; i < num_components; i++) {
         unsigned chan_component = (component + i) % 4;
         unsigned chan_idx = idx + (component + i) / 4;
         vec->operands[i] = Operand(bld.tmp(instr->def.bit_size == 16 ? v2b : v1));
         emit_interp_mov_instr(ctx, chan_idx, chan_component, vertex_id, vec->operands[i].getTemp(),
                               prim_mask, high_16bits);
      }
      vec->definitions[0] = Definition(dst);
      bld.insert(std::move(vec));
   }
}

void
visit_load_tcs_per_vertex_input(isel_context* ctx, nir_intrinsic_instr* instr)
{
   assert(ctx->shader->info.stage == MESA_SHADER_TESS_CTRL);

   Builder bld(ctx->program, ctx->block);
   Temp dst = get_ssa_temp(ctx, &instr->def);

   if (load_input_from_temps(ctx, instr, dst))
      return;

   UNREACHABLE("LDS-based TCS input should have been lowered in NIR.");
}

void
visit_load_per_vertex_input(isel_context* ctx, nir_intrinsic_instr* instr)
{
   switch (ctx->shader->info.stage) {
   case MESA_SHADER_TESS_CTRL: visit_load_tcs_per_vertex_input(ctx, instr); break;
   default: UNREACHABLE("Unimplemented shader stage");
   }
}

ac_hw_cache_flags
get_cache_flags(isel_context* ctx, unsigned access, enum ac_access_type type)
{
   return ac_get_hw_cache_flags(ctx->program->gfx_level, (gl_access_qualifier)access, type);
}

ac_hw_cache_flags
get_atomic_cache_flags(isel_context* ctx, bool return_previous)
{
   ac_hw_cache_flags cache = get_cache_flags(ctx, 0, ac_access_type_atomic);
   if (return_previous && ctx->program->gfx_level >= GFX12)
      cache.gfx12.temporal_hint |= gfx12_atomic_return;
   else if (return_previous)
      cache.value |= ac_glc;
   return cache;
}

void
load_buffer(isel_context* ctx, unsigned num_components, unsigned component_size, Temp dst,
            Temp rsrc, Temp offset, unsigned align_mul, unsigned align_offset, unsigned access,
            memory_sync_info sync = memory_sync_info())
{
   Builder bld(ctx->program, ctx->block);

   bool use_smem = access & ACCESS_SMEM_AMD;
   if (use_smem) {
      assert(component_size >= 4 ||
             (num_components * component_size <= 2 && ctx->program->gfx_level >= GFX12));
      offset = bld.as_uniform(offset);
   } else {
      /* GFX6-7 are affected by a hw bug that prevents address clamping to
       * work correctly when the SGPR offset is used.
       */
      if (offset.type() == RegType::sgpr && ctx->options->gfx_level < GFX8)
         offset = as_vgpr(ctx, offset);
   }

   LoadEmitInfo info = {Operand(offset), dst, num_components, component_size, rsrc};
   info.cache = get_cache_flags(ctx, access, ac_access_type_load);
   info.sync = sync;
   info.align_mul = align_mul;
   info.align_offset = align_offset;
   info.disable_wqm = access & ACCESS_SKIP_HELPERS;
   if (use_smem)
      emit_load(ctx, bld, info, smem_load_params);
   else
      emit_load(ctx, bld, info, mubuf_load_params);
}

void
visit_load_ubo(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Temp dst = get_ssa_temp(ctx, &instr->def);
   Builder bld(ctx->program, ctx->block);
   Temp rsrc = bld.as_uniform(get_ssa_temp(ctx, instr->src[0].ssa));

   unsigned size = instr->def.bit_size / 8;
   load_buffer(ctx, instr->num_components, size, dst, rsrc, get_ssa_temp(ctx, instr->src[1].ssa),
               nir_intrinsic_align_mul(instr), nir_intrinsic_align_offset(instr),
               nir_intrinsic_access(instr) | ACCESS_CAN_REORDER);
}

void
visit_load_constant(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Temp dst = get_ssa_temp(ctx, &instr->def);

   Builder bld(ctx->program, ctx->block);

   uint32_t desc[4];
   ac_build_raw_buffer_descriptor(ctx->options->gfx_level, 0, 0, desc);

   unsigned base = nir_intrinsic_base(instr);
   unsigned range = nir_intrinsic_range(instr);

   Temp offset = get_ssa_temp(ctx, instr->src[0].ssa);
   if (base && offset.type() == RegType::sgpr)
      offset = bld.nuw().sop2(aco_opcode::s_add_u32, bld.def(s1), bld.def(s1, scc), offset,
                              Operand::c32(base));
   else if (base && offset.type() == RegType::vgpr)
      offset = bld.vadd32(bld.def(v1), Operand::c32(base), offset);

   Temp rsrc = bld.pseudo(aco_opcode::p_create_vector, bld.def(s4),
                          bld.pseudo(aco_opcode::p_constaddr, bld.def(s2), bld.def(s1, scc),
                                     Operand::c32(ctx->constant_data_offset)),
                          Operand::c32(MIN2(base + range, ctx->shader->constant_data_size)),
                          Operand::c32(desc[3]));
   unsigned size = instr->def.bit_size / 8;
   load_buffer(ctx, instr->num_components, size, dst, rsrc, offset, nir_intrinsic_align_mul(instr),
               nir_intrinsic_align_offset(instr), nir_intrinsic_access(instr) | ACCESS_CAN_REORDER);
}

int
image_type_to_components_count(enum glsl_sampler_dim dim, bool array)
{
   switch (dim) {
   case GLSL_SAMPLER_DIM_BUF: return 1;
   case GLSL_SAMPLER_DIM_1D: return array ? 2 : 1;
   case GLSL_SAMPLER_DIM_2D: return array ? 3 : 2;
   case GLSL_SAMPLER_DIM_MS: return array ? 3 : 2;
   case GLSL_SAMPLER_DIM_3D:
   case GLSL_SAMPLER_DIM_CUBE: return 3;
   case GLSL_SAMPLER_DIM_RECT:
   case GLSL_SAMPLER_DIM_SUBPASS: return 2;
   case GLSL_SAMPLER_DIM_SUBPASS_MS: return 2;
   default: break;
   }
   return 0;
}

void
visit_bvh64_intersect_ray_amd(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   Temp dst = get_ssa_temp(ctx, &instr->def);
   Temp resource = get_ssa_temp(ctx, instr->src[0].ssa);
   Temp node = get_ssa_temp(ctx, instr->src[1].ssa);
   Temp tmax = get_ssa_temp(ctx, instr->src[2].ssa);
   Temp origin = get_ssa_temp(ctx, instr->src[3].ssa);
   Temp dir = get_ssa_temp(ctx, instr->src[4].ssa);
   Temp inv_dir = get_ssa_temp(ctx, instr->src[5].ssa);

   /* On GFX11+ image_bvh64_intersect_ray has a special vaddr layout with NSA:
    * There are five smaller vector groups:
    * node_pointer, ray_extent, ray_origin, ray_dir, ray_inv_dir.
    * These directly match the NIR intrinsic sources.
    */
   std::vector<Temp> args = {
      node, tmax, origin, dir, inv_dir,
   };

   /* Use vector-aligned scalar operands in order to avoid unnecessary copies
    * when creating vectors.
    */
   std::vector<Operand> scalar_args;
   for (Temp tmp : args) {
      for (unsigned i = 0; i < tmp.size(); i++) {
         scalar_args.push_back(Operand(emit_extract_vector(ctx, tmp, i, v1)));
         if (bld.program->gfx_level >= GFX11 || bld.program->gfx_level < GFX10_3)
            scalar_args.back().setVectorAligned(true);
      }
      /* GFX10: cannot use NSA and must treat all Operands as one large vector. */
      scalar_args.back().setVectorAligned(bld.program->gfx_level < GFX10_3);
   }
   scalar_args.back().setVectorAligned(false);

   Instruction* mimg = create_instruction(aco_opcode::image_bvh64_intersect_ray, Format::MIMG,
                                          3 + scalar_args.size(), 1);
   mimg->definitions[0] = Definition(dst);
   mimg->operands[0] = Operand(resource);
   mimg->operands[1] = Operand(s4);
   mimg->operands[2] = Operand(v1);
   for (unsigned i = 0; i < scalar_args.size(); i++)
      mimg->operands[3 + i] = scalar_args[i];

   mimg->mimg().dim = ac_image_1d;
   mimg->mimg().dmask = 0xf;
   mimg->mimg().unrm = true;
   mimg->mimg().r128 = true;
   bld.insert(std::move(mimg));

   emit_split_vector(ctx, dst, instr->def.num_components);
}

void
visit_bvh8_intersect_ray_amd(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   Temp dst = get_ssa_temp(ctx, &instr->def);
   Temp resource = get_ssa_temp(ctx, instr->src[0].ssa);
   Temp bvh_base = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[1].ssa));
   Temp cull_mask = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[2].ssa));
   Temp tmax = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[3].ssa));
   Temp origin = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[4].ssa));
   Temp dir = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[5].ssa));
   Temp node_id = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[6].ssa));

   Temp result = bld.tmp(v10);
   Temp new_origin = bld.tmp(v3);
   Temp new_dir = bld.tmp(v3);

   std::vector<Temp> args = {bvh_base, tmax, cull_mask, origin, dir, node_id};

   /* Use vector-aligned scalar operands in order to avoid unnecessary copies
    * when creating vectors.
    */
   std::vector<Operand> scalar_args;
   for (unsigned i = 0; i < args.size(); ++i) {
      Temp tmp = args[i];
      for (unsigned j = 0; j < tmp.size(); j++) {
         scalar_args.emplace_back(emit_extract_vector(ctx, tmp, j, v1));
         scalar_args.back().setVectorAligned(true);
      }
      /* (tmax, cull_mask) is passed as one vector */
      if (i != 1)
         scalar_args.back().setVectorAligned(false);
   }

   Instruction* mimg = create_instruction(aco_opcode::image_bvh8_intersect_ray, Format::MIMG,
                                          3 + scalar_args.size(), 3);
   mimg->definitions[0] = Definition(new_origin);
   mimg->definitions[1] = Definition(new_dir);
   mimg->definitions[2] = Definition(result);
   mimg->operands[0] = Operand(resource);
   mimg->operands[1] = Operand(s4);
   mimg->operands[2] = Operand(v1);
   for (unsigned i = 0; i < scalar_args.size(); i++)
      mimg->operands[3 + i] = scalar_args[i];

   mimg->mimg().dim = ac_image_1d;
   mimg->mimg().dmask = 0xf;
   mimg->mimg().unrm = true;
   mimg->mimg().r128 = true;
   bld.insert(std::move(mimg));
   emit_split_vector(ctx, result, 10);
   emit_split_vector(ctx, new_origin, 3);
   emit_split_vector(ctx, new_dir, 3);

   Temp vec[16];
   for (unsigned i = 0; i < 10; ++i)
      vec[i] = emit_extract_vector(ctx, result, i, RegClass::v1);
   for (unsigned i = 0; i < 3; ++i) {
      vec[10 + i] = emit_extract_vector(ctx, new_origin, i, RegClass::v1);
      vec[13 + i] = emit_extract_vector(ctx, new_dir, i, RegClass::v1);
   }
   create_vec_from_array(ctx, vec, 16, RegType::vgpr, 4, 0, dst);
}

static std::vector<Temp>
get_image_coords(isel_context* ctx, const nir_intrinsic_instr* instr)
{

   Temp src0 = get_ssa_temp(ctx, instr->src[1].ssa);
   bool a16 = instr->src[1].ssa->bit_size == 16;
   RegClass rc = a16 ? v2b : v1;
   enum glsl_sampler_dim dim = nir_intrinsic_image_dim(instr);
   bool is_array = nir_intrinsic_image_array(instr);
   ASSERTED bool add_frag_pos =
      (dim == GLSL_SAMPLER_DIM_SUBPASS || dim == GLSL_SAMPLER_DIM_SUBPASS_MS);
   assert(!add_frag_pos && "Input attachments should be lowered.");
   bool is_ms = (dim == GLSL_SAMPLER_DIM_MS || dim == GLSL_SAMPLER_DIM_SUBPASS_MS);
   bool gfx9_1d = ctx->options->gfx_level == GFX9 && dim == GLSL_SAMPLER_DIM_1D;
   int count = image_type_to_components_count(dim, is_array);
   std::vector<Temp> coords;
   Builder bld(ctx->program, ctx->block);

   if (gfx9_1d) {
      coords.emplace_back(emit_extract_vector(ctx, src0, 0, rc));
      coords.emplace_back(bld.copy(bld.def(rc), Operand::zero(a16 ? 2 : 4)));
      if (is_array)
         coords.emplace_back(emit_extract_vector(ctx, src0, 1, rc));
   } else {
      for (int i = 0; i < count; i++)
         coords.emplace_back(emit_extract_vector(ctx, src0, i, rc));
   }

   bool has_lod = false;
   Temp lod;

   if (instr->intrinsic == nir_intrinsic_bindless_image_load ||
       instr->intrinsic == nir_intrinsic_bindless_image_sparse_load ||
       instr->intrinsic == nir_intrinsic_bindless_image_store) {
      int lod_index = instr->intrinsic == nir_intrinsic_bindless_image_store ? 4 : 3;
      assert(instr->src[lod_index].ssa->bit_size == (a16 ? 16 : 32));
      has_lod =
         !nir_src_is_const(instr->src[lod_index]) || nir_src_as_uint(instr->src[lod_index]) != 0;

      if (has_lod)
         lod = get_ssa_temp_tex(ctx, instr->src[lod_index].ssa, a16);
   }

   if (ctx->program->info.image_2d_view_of_3d && dim == GLSL_SAMPLER_DIM_2D && !is_array) {
      /* The hw can't bind a slice of a 3D image as a 2D image, because it
       * ignores BASE_ARRAY if the target is 3D. The workaround is to read
       * BASE_ARRAY and set it as the 3rd address operand for all 2D images.
       */
      assert(ctx->options->gfx_level == GFX9);
      Temp rsrc = bld.as_uniform(get_ssa_temp(ctx, instr->src[0].ssa));
      Temp rsrc_word5 = emit_extract_vector(ctx, rsrc, 5, v1);
      /* Extract the BASE_ARRAY field [0:12] from the descriptor. */
      Temp first_layer = bld.vop3(aco_opcode::v_bfe_u32, bld.def(v1), rsrc_word5, Operand::c32(0u),
                                  Operand::c32(13u));

      if (has_lod) {
         /* If there's a lod parameter it matter if the image is 3d or 2d because
          * the hw reads either the fourth or third component as lod. So detect
          * 3d images and place the lod at the third component otherwise.
          * For non 3D descriptors we effectively add lod twice to coords,
          * but the hw will only read the first one, the second is ignored.
          */
         Temp rsrc_word3 = emit_extract_vector(ctx, rsrc, 3, s1);
         Temp type = bld.sop2(aco_opcode::s_bfe_u32, bld.def(s1), bld.def(s1, scc), rsrc_word3,
                              Operand::c32(28 | (4 << 16))); /* extract last 4 bits */
         Temp is_3d = bld.vopc_e64(aco_opcode::v_cmp_eq_u32, bld.def(bld.lm), type,
                                   Operand::c32(V_008F1C_SQ_RSRC_IMG_3D));
         first_layer =
            bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), as_vgpr(ctx, lod), first_layer, is_3d);
      }

      if (a16)
         coords.emplace_back(emit_extract_vector(ctx, first_layer, 0, v2b));
      else
         coords.emplace_back(first_layer);
   }

   if (is_ms && instr->intrinsic != nir_intrinsic_bindless_image_fragment_mask_load_amd) {
      assert(instr->src[2].ssa->bit_size == (a16 ? 16 : 32));
      coords.emplace_back(get_ssa_temp_tex(ctx, instr->src[2].ssa, a16));
   }

   if (has_lod)
      coords.emplace_back(lod);

   return emit_pack_v1(ctx, coords);
}

memory_sync_info
get_memory_sync_info(nir_intrinsic_instr* instr, storage_class storage, unsigned semantics)
{
   /* atomicrmw might not have NIR_INTRINSIC_ACCESS and there's nothing interesting there anyway */
   if (semantics & semantic_atomicrmw)
      return memory_sync_info(storage, semantics);

   unsigned access = nir_intrinsic_access(instr);

   if (access & ACCESS_VOLATILE)
      semantics |= semantic_volatile;
   if (access & ACCESS_CAN_REORDER)
      semantics |= semantic_can_reorder | semantic_private;
   else if (access & ACCESS_ATOMIC)
      semantics |= semantic_atomic;

   return memory_sync_info(storage, semantics);
}

void
visit_image_load(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   const enum glsl_sampler_dim dim = nir_intrinsic_image_dim(instr);
   bool is_array = nir_intrinsic_image_array(instr);
   bool is_sparse = instr->intrinsic == nir_intrinsic_bindless_image_sparse_load;
   Temp dst = get_ssa_temp(ctx, &instr->def);

   memory_sync_info sync = get_memory_sync_info(instr, storage_image, 0);

   unsigned result_size = instr->def.num_components - is_sparse;
   unsigned expand_mask = nir_def_components_read(&instr->def) & u_bit_consecutive(0, result_size);
   expand_mask = MAX2(expand_mask, 1); /* this can be zero in the case of sparse image loads */
   if (dim == GLSL_SAMPLER_DIM_BUF)
      expand_mask = (1u << util_last_bit(expand_mask)) - 1u;
   unsigned dmask = expand_mask;
   if (instr->def.bit_size == 64) {
      expand_mask &= 0x9;
      /* only R64_UINT and R64_SINT supported. x is in xy of the result, w in zw */
      dmask = ((expand_mask & 0x1) ? 0x3 : 0) | ((expand_mask & 0x8) ? 0xc : 0);
   }
   if (is_sparse)
      expand_mask |= 1 << result_size;

   bool d16 = instr->def.bit_size == 16;
   assert(!d16 || !is_sparse);

   unsigned num_bytes = util_bitcount(dmask) * (d16 ? 2 : 4) + is_sparse * 4;

   Temp tmp;
   if (num_bytes == dst.bytes() && dst.type() == RegType::vgpr)
      tmp = dst;
   else
      tmp = bld.tmp(RegClass::get(RegType::vgpr, num_bytes));

   Temp resource = bld.as_uniform(get_ssa_temp(ctx, instr->src[0].ssa));

   enum gl_access_qualifier access = nir_intrinsic_access(instr);
   bool disable_wqm = access & ACCESS_SKIP_HELPERS;

   if (dim == GLSL_SAMPLER_DIM_BUF) {
      Temp vindex = emit_extract_vector(ctx, get_ssa_temp(ctx, instr->src[1].ssa), 0, v1);

      aco_opcode opcode;
      if (!d16) {
         switch (util_bitcount(dmask)) {
         case 1: opcode = aco_opcode::buffer_load_format_x; break;
         case 2: opcode = aco_opcode::buffer_load_format_xy; break;
         case 3: opcode = aco_opcode::buffer_load_format_xyz; break;
         case 4: opcode = aco_opcode::buffer_load_format_xyzw; break;
         default: UNREACHABLE(">4 channel buffer image load");
         }
      } else {
         switch (util_bitcount(dmask)) {
         case 1: opcode = aco_opcode::buffer_load_format_d16_x; break;
         case 2: opcode = aco_opcode::buffer_load_format_d16_xy; break;
         case 3: opcode = aco_opcode::buffer_load_format_d16_xyz; break;
         case 4: opcode = aco_opcode::buffer_load_format_d16_xyzw; break;
         default: UNREACHABLE(">4 channel buffer image load");
         }
      }
      aco_ptr<Instruction> load{
         create_instruction(opcode, Format::MUBUF, 3 + is_sparse + 2 * disable_wqm, 1)};
      load->operands[0] = Operand(resource);
      load->operands[1] = Operand(vindex);
      load->operands[2] = Operand::c32(0);
      load->definitions[0] = Definition(tmp);
      load->mubuf().idxen = true;
      load->mubuf().cache = get_cache_flags(ctx, access, ac_access_type_load);
      load->mubuf().sync = sync;
      load->mubuf().tfe = is_sparse;
      if (load->mubuf().tfe)
         load->operands[3] = emit_tfe_init(bld, tmp);
      init_disable_wqm(bld, load->mubuf(), disable_wqm);
      ctx->block->instructions.emplace_back(std::move(load));
   } else {
      std::vector<Temp> coords = get_image_coords(ctx, instr);

      aco_opcode opcode;
      if (instr->intrinsic == nir_intrinsic_bindless_image_fragment_mask_load_amd) {
         opcode = aco_opcode::image_load;
      } else {
         bool level_zero = nir_src_is_const(instr->src[3]) && nir_src_as_uint(instr->src[3]) == 0;
         opcode = level_zero ? aco_opcode::image_load : aco_opcode::image_load_mip;
      }

      Operand vdata = is_sparse ? emit_tfe_init(bld, tmp) : Operand(v1);
      MIMG_instruction* load =
         emit_mimg(bld, opcode, {tmp}, resource, Operand(s4), coords, disable_wqm, vdata);
      load->cache = get_cache_flags(ctx, access, ac_access_type_load);
      load->a16 = instr->src[1].ssa->bit_size == 16;
      load->d16 = d16;
      load->dmask = dmask;
      load->unrm = true;
      load->tfe = is_sparse;

      if (instr->intrinsic == nir_intrinsic_bindless_image_fragment_mask_load_amd) {
         load->dim = is_array ? ac_image_2darray : ac_image_2d;
         load->da = is_array;
         load->sync = memory_sync_info();
      } else {
         ac_image_dim sdim = ac_get_image_dim(ctx->options->gfx_level, dim, is_array);
         load->dim = sdim;
         load->da = should_declare_array(sdim);
         load->sync = sync;
      }
   }

   if (is_sparse && instr->def.bit_size == 64) {
      /* The result components are 64-bit but the sparse residency code is
       * 32-bit. So add a zero to the end so expand_vector() works correctly.
       */
      tmp = bld.pseudo(aco_opcode::p_create_vector, bld.def(RegType::vgpr, tmp.size() + 1), tmp,
                       Operand::zero());
   }

   expand_vector(ctx, tmp, dst, instr->def.num_components, expand_mask, instr->def.bit_size == 64);
}

void
visit_image_store(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   const enum glsl_sampler_dim dim = nir_intrinsic_image_dim(instr);
   bool is_array = nir_intrinsic_image_array(instr);
   Temp data = get_ssa_temp(ctx, instr->src[3].ssa);
   bool d16 = instr->src[3].ssa->bit_size == 16;

   /* only R64_UINT and R64_SINT supported */
   if (instr->src[3].ssa->bit_size == 64 && data.bytes() > 8)
      data = emit_extract_vector(ctx, data, 0, RegClass(data.type(), 2));
   data = as_vgpr(ctx, data);

   uint32_t num_components = d16 ? instr->src[3].ssa->num_components : data.size();

   memory_sync_info sync = get_memory_sync_info(instr, storage_image, 0);
   unsigned access = nir_intrinsic_access(instr);
   ac_hw_cache_flags cache = get_cache_flags(ctx, access, ac_access_type_store_subdword);

   uint32_t dmask = BITFIELD_MASK(num_components);
   if (instr->src[3].ssa->bit_size == 32 || instr->src[3].ssa->bit_size == 16) {
      for (uint32_t i = 0; i < instr->num_components; i++) {
         /* components not in dmask receive:
          * GFX6-11.5:  zero
          * GFX12+: first component in dmask
          */
         nir_scalar comp = nir_scalar_resolved(instr->src[3].ssa, i);
         if (nir_scalar_is_undef(comp)) {
            dmask &= ~BITFIELD_BIT(i);
         } else if (ctx->options->gfx_level <= GFX11_5) {
            if (nir_scalar_is_const(comp) && nir_scalar_as_uint(comp) == 0)
               dmask &= ~BITFIELD_BIT(i);
         } else {
            unsigned first = dim == GLSL_SAMPLER_DIM_BUF ? 0 : ffs(dmask) - 1;
            if (i != first && nir_scalar_equal(nir_scalar_resolved(instr->src[3].ssa, first), comp))
               dmask &= ~BITFIELD_BIT(i);
         }
      }

      /* dmask cannot be 0, at least one vgpr is always read */
      if (dmask == 0)
         dmask = 1;
      /* buffer store only supports consecutive components. */
      if (dim == GLSL_SAMPLER_DIM_BUF)
         dmask = BITFIELD_MASK(util_last_bit(dmask));

      if (dmask != BITFIELD_MASK(num_components)) {
         uint32_t dmask_count = util_bitcount(dmask);
         RegClass rc = d16 ? v2b : v1;
         if (dmask_count == 1) {
            data = emit_extract_vector(ctx, data, ffs(dmask) - 1, rc);
         } else {
            aco_ptr<Instruction> vec{
               create_instruction(aco_opcode::p_create_vector, Format::PSEUDO, dmask_count, 1)};
            uint32_t index = 0;
            u_foreach_bit (bit, dmask) {
               vec->operands[index++] = Operand(emit_extract_vector(ctx, data, bit, rc));
            }
            data = bld.tmp(RegClass::get(RegType::vgpr, dmask_count * rc.bytes()));
            vec->definitions[0] = Definition(data);
            bld.insert(std::move(vec));
         }
      }
   }

   if (dim == GLSL_SAMPLER_DIM_BUF) {
      Temp rsrc = bld.as_uniform(get_ssa_temp(ctx, instr->src[0].ssa));
      Temp vindex = emit_extract_vector(ctx, get_ssa_temp(ctx, instr->src[1].ssa), 0, v1);
      aco_opcode opcode;
      if (!d16) {
         switch (dmask) {
         case 0x1: opcode = aco_opcode::buffer_store_format_x; break;
         case 0x3: opcode = aco_opcode::buffer_store_format_xy; break;
         case 0x7: opcode = aco_opcode::buffer_store_format_xyz; break;
         case 0xf: opcode = aco_opcode::buffer_store_format_xyzw; break;
         default: UNREACHABLE(">4 channel buffer image store");
         }
      } else {
         switch (dmask) {
         case 0x1: opcode = aco_opcode::buffer_store_format_d16_x; break;
         case 0x3: opcode = aco_opcode::buffer_store_format_d16_xy; break;
         case 0x7: opcode = aco_opcode::buffer_store_format_d16_xyz; break;
         case 0xf: opcode = aco_opcode::buffer_store_format_d16_xyzw; break;
         default: UNREACHABLE(">4 channel buffer image store");
         }
      }
      aco_ptr<Instruction> store{create_instruction(opcode, Format::MUBUF, 6, 0)};
      store->operands[0] = Operand(rsrc);
      store->operands[1] = Operand(vindex);
      store->operands[2] = Operand::c32(0);
      store->operands[3] = Operand(data);
      store->mubuf().idxen = true;
      store->mubuf().cache = cache;
      store->mubuf().sync = sync;
      init_disable_wqm(bld, store->mubuf(), true);
      ctx->block->instructions.emplace_back(std::move(store));
      return;
   }

   assert(data.type() == RegType::vgpr);
   std::vector<Temp> coords = get_image_coords(ctx, instr);
   Temp resource = bld.as_uniform(get_ssa_temp(ctx, instr->src[0].ssa));

   bool level_zero = nir_src_is_const(instr->src[4]) && nir_src_as_uint(instr->src[4]) == 0;
   aco_opcode opcode = level_zero ? aco_opcode::image_store : aco_opcode::image_store_mip;

   MIMG_instruction* store =
      emit_mimg(bld, opcode, {}, resource, Operand(s4), coords, true, Operand(data));
   store->cache = cache;
   store->a16 = instr->src[1].ssa->bit_size == 16;
   store->d16 = d16;
   store->dmask = dmask;
   store->unrm = true;
   ac_image_dim sdim = ac_get_image_dim(ctx->options->gfx_level, dim, is_array);
   store->dim = sdim;
   store->da = should_declare_array(sdim);
   store->sync = sync;
   return;
}

void
translate_buffer_image_atomic_op(const nir_atomic_op op, aco_opcode* buf_op, aco_opcode* buf_op64,
                                 aco_opcode* image_op)
{
   switch (op) {
   case nir_atomic_op_iadd:
      *buf_op = aco_opcode::buffer_atomic_add;
      *buf_op64 = aco_opcode::buffer_atomic_add_x2;
      *image_op = aco_opcode::image_atomic_add;
      break;
   case nir_atomic_op_isub:
      *buf_op = aco_opcode::buffer_atomic_sub;
      *buf_op64 = aco_opcode::buffer_atomic_sub_x2;
      *image_op = aco_opcode::image_atomic_sub;
      break;
   case nir_atomic_op_umin:
      *buf_op = aco_opcode::buffer_atomic_umin;
      *buf_op64 = aco_opcode::buffer_atomic_umin_x2;
      *image_op = aco_opcode::image_atomic_umin;
      break;
   case nir_atomic_op_imin:
      *buf_op = aco_opcode::buffer_atomic_smin;
      *buf_op64 = aco_opcode::buffer_atomic_smin_x2;
      *image_op = aco_opcode::image_atomic_smin;
      break;
   case nir_atomic_op_umax:
      *buf_op = aco_opcode::buffer_atomic_umax;
      *buf_op64 = aco_opcode::buffer_atomic_umax_x2;
      *image_op = aco_opcode::image_atomic_umax;
      break;
   case nir_atomic_op_imax:
      *buf_op = aco_opcode::buffer_atomic_smax;
      *buf_op64 = aco_opcode::buffer_atomic_smax_x2;
      *image_op = aco_opcode::image_atomic_smax;
      break;
   case nir_atomic_op_iand:
      *buf_op = aco_opcode::buffer_atomic_and;
      *buf_op64 = aco_opcode::buffer_atomic_and_x2;
      *image_op = aco_opcode::image_atomic_and;
      break;
   case nir_atomic_op_ior:
      *buf_op = aco_opcode::buffer_atomic_or;
      *buf_op64 = aco_opcode::buffer_atomic_or_x2;
      *image_op = aco_opcode::image_atomic_or;
      break;
   case nir_atomic_op_ixor:
      *buf_op = aco_opcode::buffer_atomic_xor;
      *buf_op64 = aco_opcode::buffer_atomic_xor_x2;
      *image_op = aco_opcode::image_atomic_xor;
      break;
   case nir_atomic_op_xchg:
      *buf_op = aco_opcode::buffer_atomic_swap;
      *buf_op64 = aco_opcode::buffer_atomic_swap_x2;
      *image_op = aco_opcode::image_atomic_swap;
      break;
   case nir_atomic_op_cmpxchg:
      *buf_op = aco_opcode::buffer_atomic_cmpswap;
      *buf_op64 = aco_opcode::buffer_atomic_cmpswap_x2;
      *image_op = aco_opcode::image_atomic_cmpswap;
      break;
   case nir_atomic_op_inc_wrap:
      *buf_op = aco_opcode::buffer_atomic_inc;
      *buf_op64 = aco_opcode::buffer_atomic_inc_x2;
      *image_op = aco_opcode::image_atomic_inc;
      break;
   case nir_atomic_op_dec_wrap:
      *buf_op = aco_opcode::buffer_atomic_dec;
      *buf_op64 = aco_opcode::buffer_atomic_dec_x2;
      *image_op = aco_opcode::image_atomic_dec;
      break;
   case nir_atomic_op_fadd:
      *buf_op = aco_opcode::buffer_atomic_add_f32;
      *buf_op64 = aco_opcode::num_opcodes;
      *image_op = aco_opcode::image_atomic_add_flt;
      break;
   case nir_atomic_op_fmin:
      *buf_op = aco_opcode::buffer_atomic_fmin;
      *buf_op64 = aco_opcode::buffer_atomic_fmin_x2;
      *image_op = aco_opcode::image_atomic_fmin;
      break;
   case nir_atomic_op_fmax:
      *buf_op = aco_opcode::buffer_atomic_fmax;
      *buf_op64 = aco_opcode::buffer_atomic_fmax_x2;
      *image_op = aco_opcode::image_atomic_fmax;
      break;
   default: UNREACHABLE("unsupported atomic operation");
   }
}

void
visit_image_atomic(isel_context* ctx, nir_intrinsic_instr* instr)
{
   bool return_previous = !nir_def_is_unused(&instr->def);
   const enum glsl_sampler_dim dim = nir_intrinsic_image_dim(instr);
   bool is_array = nir_intrinsic_image_array(instr);
   Builder bld(ctx->program, ctx->block);

   const nir_atomic_op op = nir_intrinsic_atomic_op(instr);
   const bool cmpswap = op == nir_atomic_op_cmpxchg;

   aco_opcode buf_op, buf_op64, image_op;
   translate_buffer_image_atomic_op(op, &buf_op, &buf_op64, &image_op);

   Temp data = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[3].ssa));
   bool is_64bit = data.bytes() == 8;
   assert((data.bytes() == 4 || data.bytes() == 8) && "only 32/64-bit image atomics implemented.");

   if (cmpswap)
      data = bld.pseudo(aco_opcode::p_create_vector, bld.def(is_64bit ? v4 : v2),
                        get_ssa_temp(ctx, instr->src[4].ssa), data);

   Temp dst = get_ssa_temp(ctx, &instr->def);
   memory_sync_info sync = get_memory_sync_info(instr, storage_image, semantic_atomicrmw);

   if (dim == GLSL_SAMPLER_DIM_BUF) {
      Temp vindex = emit_extract_vector(ctx, get_ssa_temp(ctx, instr->src[1].ssa), 0, v1);
      Temp resource = bld.as_uniform(get_ssa_temp(ctx, instr->src[0].ssa));
      // assert(ctx->options->gfx_level < GFX9 && "GFX9 stride size workaround not yet
      // implemented.");
      aco_ptr<Instruction> mubuf{create_instruction(is_64bit ? buf_op64 : buf_op, Format::MUBUF, 6,
                                                    return_previous ? 1 : 0)};
      mubuf->operands[0] = Operand(resource);
      mubuf->operands[1] = Operand(vindex);
      mubuf->operands[2] = Operand::c32(0);
      mubuf->operands[3] = Operand(data);
      Definition def =
         return_previous ? (cmpswap ? bld.def(data.regClass()) : Definition(dst)) : Definition();
      if (return_previous)
         mubuf->definitions[0] = def;
      mubuf->mubuf().offset = 0;
      mubuf->mubuf().idxen = true;
      mubuf->mubuf().cache = get_atomic_cache_flags(ctx, return_previous);
      mubuf->mubuf().sync = sync;
      init_disable_wqm(bld, mubuf->mubuf(), true);
      ctx->block->instructions.emplace_back(std::move(mubuf));
      if (return_previous && cmpswap)
         bld.pseudo(aco_opcode::p_extract_vector, Definition(dst), def.getTemp(), Operand::zero());
      return;
   }

   std::vector<Temp> coords = get_image_coords(ctx, instr);
   Temp resource = bld.as_uniform(get_ssa_temp(ctx, instr->src[0].ssa));
   std::vector<Temp> tmps;
   if (return_previous)
      tmps = {(cmpswap ? bld.tmp(data.regClass()) : dst)};
   MIMG_instruction* mimg =
      emit_mimg(bld, image_op, tmps, resource, Operand(s4), coords, true, Operand(data));
   mimg->cache = get_atomic_cache_flags(ctx, return_previous);
   mimg->dmask = (1 << data.size()) - 1;
   mimg->a16 = instr->src[1].ssa->bit_size == 16;
   mimg->unrm = true;
   ac_image_dim sdim = ac_get_image_dim(ctx->options->gfx_level, dim, is_array);
   mimg->dim = sdim;
   mimg->da = should_declare_array(sdim);
   mimg->sync = sync;
   if (return_previous && cmpswap)
      bld.pseudo(aco_opcode::p_extract_vector, Definition(dst), tmps[0], Operand::zero());
   return;
}

void
visit_load_ssbo(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   unsigned num_components = instr->num_components;

   Temp dst = get_ssa_temp(ctx, &instr->def);
   Temp rsrc = bld.as_uniform(get_ssa_temp(ctx, instr->src[0].ssa));

   unsigned access = nir_intrinsic_access(instr);
   unsigned size = instr->def.bit_size / 8;

   load_buffer(ctx, num_components, size, dst, rsrc, get_ssa_temp(ctx, instr->src[1].ssa),
               nir_intrinsic_align_mul(instr), nir_intrinsic_align_offset(instr), access,
               get_memory_sync_info(instr, storage_buffer, 0));
}

void
visit_store_ssbo(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   Temp data = get_ssa_temp(ctx, instr->src[0].ssa);
   unsigned elem_size_bytes = instr->src[0].ssa->bit_size / 8;
   unsigned writemask = util_widen_mask(nir_intrinsic_write_mask(instr), elem_size_bytes);
   Temp offset = get_ssa_temp(ctx, instr->src[2].ssa);

   Temp rsrc = bld.as_uniform(get_ssa_temp(ctx, instr->src[1].ssa));

   memory_sync_info sync = get_memory_sync_info(instr, storage_buffer, 0);

   /* 64-bit atomic stores need to be at most 8 bytes so that they can use MTBUF for correct bounds
    * checking. */
   bool use_mtbuf =
      (sync.semantics & semantic_atomic) && elem_size_bytes == 8 && nir_intrinsic_align(instr) >= 8;
   unsigned max_size = use_mtbuf ? 8 : 16;

   unsigned write_count = 0;
   Temp write_datas[32];
   unsigned offsets[32];
   split_buffer_store(ctx, instr, false, RegType::vgpr, data, writemask, max_size, &write_count,
                      write_datas, offsets);

   /* GFX6-7 are affected by a hw bug that prevents address clamping to work
    * correctly when the SGPR offset is used.
    */
   if (offset.type() == RegType::sgpr && ctx->options->gfx_level < GFX8)
      offset = as_vgpr(ctx, offset);

   for (unsigned i = 0; i < write_count; i++) {
      aco_opcode op = get_buffer_store_op(write_datas[i].bytes());
      unsigned access = nir_intrinsic_access(instr);
      enum ac_access_type type = ac_access_type_store;
      if (write_datas[i].bytes() < 4)
         type = ac_access_type_store_subdword;

      if (use_mtbuf) {
         assert(write_datas[i].bytes() == 8);
         op = aco_opcode::tbuffer_store_format_xy;
      }

      aco_ptr<Instruction> store{
         create_instruction(op, use_mtbuf ? Format::MTBUF : Format::MUBUF, 6, 0)};
      store->operands[0] = Operand(rsrc);
      store->operands[1] = offset.type() == RegType::vgpr ? Operand(offset) : Operand(v1);
      store->operands[2] = offset.type() == RegType::sgpr ? Operand(offset) : Operand::c32(0);
      store->operands[3] = Operand(write_datas[i]);
      if (use_mtbuf) {
         store->mtbuf().offset = offsets[i];
         store->mtbuf().offen = (offset.type() == RegType::vgpr);
         store->mtbuf().cache = get_cache_flags(ctx, access, type);
         store->mtbuf().sync = sync;
         store->mtbuf().dfmt = V_008F0C_BUF_DATA_FORMAT_32_32;
         store->mtbuf().nfmt = V_008F0C_BUF_NUM_FORMAT_UINT;
         init_disable_wqm(bld, store->mtbuf(), true);
      } else {
         store->mubuf().offset = offsets[i];
         store->mubuf().offen = (offset.type() == RegType::vgpr);
         store->mubuf().cache = get_cache_flags(ctx, access, type);
         store->mubuf().sync = sync;
         init_disable_wqm(bld, store->mubuf(), true);
      }
      ctx->block->instructions.emplace_back(std::move(store));
   }
}

void
visit_atomic_ssbo(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   bool return_previous = !nir_def_is_unused(&instr->def);
   Temp data = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[2].ssa));

   const nir_atomic_op nir_op = nir_intrinsic_atomic_op(instr);
   const bool cmpswap = nir_op == nir_atomic_op_cmpxchg;

   aco_opcode op32, op64, image_op;
   translate_buffer_image_atomic_op(nir_op, &op32, &op64, &image_op);

   if (cmpswap)
      data = bld.pseudo(aco_opcode::p_create_vector, bld.def(RegType::vgpr, data.size() * 2),
                        get_ssa_temp(ctx, instr->src[3].ssa), data);

   Temp offset = get_ssa_temp(ctx, instr->src[1].ssa);
   Temp rsrc = bld.as_uniform(get_ssa_temp(ctx, instr->src[0].ssa));
   Temp dst = get_ssa_temp(ctx, &instr->def);

   aco_opcode op = instr->def.bit_size == 32 ? op32 : op64;
   aco_ptr<Instruction> mubuf{create_instruction(op, Format::MUBUF, 6, return_previous ? 1 : 0)};
   mubuf->operands[0] = Operand(rsrc);
   mubuf->operands[1] = offset.type() == RegType::vgpr ? Operand(offset) : Operand(v1);
   mubuf->operands[2] = offset.type() == RegType::sgpr ? Operand(offset) : Operand::c32(0);
   mubuf->operands[3] = Operand(data);
   Definition def =
      return_previous ? (cmpswap ? bld.def(data.regClass()) : Definition(dst)) : Definition();
   if (return_previous)
      mubuf->definitions[0] = def;
   mubuf->mubuf().offset = 0;
   mubuf->mubuf().offen = (offset.type() == RegType::vgpr);
   mubuf->mubuf().cache = get_atomic_cache_flags(ctx, return_previous);
   mubuf->mubuf().sync = get_memory_sync_info(instr, storage_buffer, semantic_atomicrmw);
   init_disable_wqm(bld, mubuf->mubuf(), true);
   ctx->block->instructions.emplace_back(std::move(mubuf));
   if (return_previous && cmpswap)
      bld.pseudo(aco_opcode::p_extract_vector, Definition(dst), def.getTemp(), Operand::zero());
}

void
parse_global(isel_context* ctx, nir_intrinsic_instr* intrin, Temp* address, uint32_t* const_offset,
             Temp* offset)
{
   bool is_store = intrin->intrinsic == nir_intrinsic_store_global_amd;
   *address = get_ssa_temp(ctx, intrin->src[is_store ? 1 : 0].ssa);
   assert(address->bytes() == 8);

   *const_offset = nir_intrinsic_base(intrin);

   unsigned num_src = nir_intrinsic_infos[intrin->intrinsic].num_srcs;
   nir_src offset_src = intrin->src[num_src - 1];
   if (!nir_src_is_const(offset_src) || nir_src_as_uint(offset_src))
      *offset = get_ssa_temp(ctx, offset_src.ssa);
   else
      *offset = Temp();
}

void
visit_load_global(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   unsigned num_components = instr->num_components;
   unsigned component_size = instr->def.bit_size / 8;
   unsigned access = nir_intrinsic_access(instr);

   Temp addr, offset;
   uint32_t const_offset;
   parse_global(ctx, instr, &addr, &const_offset, &offset);

   LoadEmitInfo info = {Operand(addr), get_ssa_temp(ctx, &instr->def), num_components,
                        component_size};
   if (offset.id()) {
      info.resource = addr;
      info.offset = Operand(offset);
   }
   info.const_offset = const_offset;
   info.align_mul = nir_intrinsic_align_mul(instr);
   info.align_offset = nir_intrinsic_align_offset(instr);
   info.sync = get_memory_sync_info(instr, storage_buffer, 0);
   info.resource_src = &instr->src[0];
   info.offset_src = offset.id() ? &instr->src[1] : NULL;
   info.cache = get_cache_flags(ctx, access, ac_access_type_load);
   info.disable_wqm = access & ACCESS_SKIP_HELPERS;

   if (access & ACCESS_SMEM_AMD) {
      assert(component_size >= 4 ||
             (num_components * component_size <= 2 && ctx->program->gfx_level >= GFX12));
      if (info.resource.id())
         info.resource = bld.as_uniform(info.resource);
      info.offset = Operand(bld.as_uniform(info.offset));
      EmitLoadParameters params = smem_load_params;
      params.max_const_offset = ctx->program->dev.smem_offset_max;
      emit_load(ctx, bld, info, params);
   } else {
      EmitLoadParameters params = global_load_params;
      emit_load(ctx, bld, info, params);
   }
}

void
visit_store_global(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   unsigned elem_size_bytes = instr->src[0].ssa->bit_size / 8;
   unsigned writemask = util_widen_mask(nir_intrinsic_write_mask(instr), elem_size_bytes);

   Temp data = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[0].ssa));
   memory_sync_info sync = get_memory_sync_info(instr, storage_buffer, 0);

   unsigned write_count = 0;
   Temp write_datas[32];
   unsigned offsets[32];
   split_buffer_store(ctx, instr, false, RegType::vgpr, data, writemask, 16, &write_count,
                      write_datas, offsets);

   Temp addr, offset;
   uint32_t const_offset;
   parse_global(ctx, instr, &addr, &const_offset, &offset);

   for (unsigned i = 0; i < write_count; i++) {
      Temp write_address = addr;
      uint32_t write_const_offset = const_offset;
      Temp write_offset = offset;
      Format format = lower_global_address(ctx, bld, offsets[i], &write_address,
                                           &write_const_offset, &write_offset, &instr->src[2]);

      unsigned access = nir_intrinsic_access(instr);
      enum ac_access_type type = ac_access_type_store;
      if (write_datas[i].bytes() < 4)
         type = ac_access_type_store_subdword;

      if (format != Format::MUBUF) {
         bool global = format == Format::GLOBAL;
         aco_opcode op;
         switch (write_datas[i].bytes()) {
         case 1: op = global ? aco_opcode::global_store_byte : aco_opcode::flat_store_byte; break;
         case 2: op = global ? aco_opcode::global_store_short : aco_opcode::flat_store_short; break;
         case 4: op = global ? aco_opcode::global_store_dword : aco_opcode::flat_store_dword; break;
         case 8:
            op = global ? aco_opcode::global_store_dwordx2 : aco_opcode::flat_store_dwordx2;
            break;
         case 12:
            op = global ? aco_opcode::global_store_dwordx3 : aco_opcode::flat_store_dwordx3;
            break;
         case 16:
            op = global ? aco_opcode::global_store_dwordx4 : aco_opcode::flat_store_dwordx4;
            break;
         default: UNREACHABLE("store_global not implemented for this size.");
         }

         aco_ptr<Instruction> flat{create_instruction(op, format, 5, 0)};
         if (write_address.regClass() == s2) {
            assert(global && write_offset.id() && write_offset.type() == RegType::vgpr);
            flat->operands[0] = Operand(write_offset);
            flat->operands[1] = Operand(write_address);
         } else {
            assert(write_address.type() == RegType::vgpr && !write_offset.id());
            flat->operands[0] = Operand(write_address);
            flat->operands[1] = Operand(s1);
         }
         flat->operands[2] = Operand(write_datas[i]);
         flat->flatlike().cache = get_cache_flags(ctx, access, type);
         assert(global || !write_const_offset);
         flat->flatlike().offset = write_const_offset;
         flat->flatlike().sync = sync;
         init_disable_wqm(bld, flat->flatlike(), true);
         ctx->block->instructions.emplace_back(std::move(flat));
      } else {
         assert(ctx->options->gfx_level == GFX6 || write_address.type() != RegType::vgpr);

         aco_opcode op = get_buffer_store_op(write_datas[i].bytes());

         Temp rsrc = get_mubuf_global_rsrc(bld, write_address);

         aco_ptr<Instruction> mubuf{create_instruction(op, Format::MUBUF, 6, 0)};
         mubuf->operands[0] = Operand(rsrc);
         if (write_address.type() == RegType::vgpr)
            mubuf->operands[1] = Operand(write_address);
         else if (write_offset.type() == RegType::vgpr)
            mubuf->operands[1] = Operand(write_offset);
         else
            mubuf->operands[1] = Operand(v1);
         mubuf->operands[2] =
            write_offset.type() == RegType::sgpr ? Operand(write_offset) : Operand::c32(0);
         mubuf->operands[3] = Operand(write_datas[i]);
         mubuf->mubuf().offen = write_offset.type() == RegType::vgpr;
         mubuf->mubuf().cache = get_cache_flags(ctx, access, type);
         mubuf->mubuf().offset = write_const_offset;
         mubuf->mubuf().addr64 = write_address.type() == RegType::vgpr;
         mubuf->mubuf().sync = sync;
         init_disable_wqm(bld, mubuf->mubuf(), true);
         ctx->block->instructions.emplace_back(std::move(mubuf));
      }
   }
}

void
visit_global_atomic(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   bool return_previous = !nir_def_is_unused(&instr->def);
   Temp data = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[1].ssa));

   const nir_atomic_op nir_op = nir_intrinsic_atomic_op(instr);
   const bool cmpswap = nir_op == nir_atomic_op_cmpxchg;
   unsigned offset_idx = 2;

   if (cmpswap) {
      offset_idx = 3;
      data = bld.pseudo(aco_opcode::p_create_vector, bld.def(RegType::vgpr, data.size() * 2),
                        get_ssa_temp(ctx, instr->src[2].ssa), data);
   }

   Temp dst = get_ssa_temp(ctx, &instr->def);

   aco_opcode op32, op64;

   Temp addr, offset;
   uint32_t const_offset;
   parse_global(ctx, instr, &addr, &const_offset, &offset);
   Format format =
      lower_global_address(ctx, bld, 0, &addr, &const_offset, &offset, &instr->src[offset_idx]);

   if (format != Format::MUBUF) {
      bool global = format == Format::GLOBAL;
      switch (nir_op) {
      case nir_atomic_op_iadd:
         op32 = global ? aco_opcode::global_atomic_add : aco_opcode::flat_atomic_add;
         op64 = global ? aco_opcode::global_atomic_add_x2 : aco_opcode::flat_atomic_add_x2;
         break;
      case nir_atomic_op_isub:
         op32 = global ? aco_opcode::global_atomic_sub : aco_opcode::flat_atomic_sub;
         op64 = global ? aco_opcode::global_atomic_sub_x2 : aco_opcode::flat_atomic_sub_x2;
         break;
      case nir_atomic_op_imin:
         op32 = global ? aco_opcode::global_atomic_smin : aco_opcode::flat_atomic_smin;
         op64 = global ? aco_opcode::global_atomic_smin_x2 : aco_opcode::flat_atomic_smin_x2;
         break;
      case nir_atomic_op_umin:
         op32 = global ? aco_opcode::global_atomic_umin : aco_opcode::flat_atomic_umin;
         op64 = global ? aco_opcode::global_atomic_umin_x2 : aco_opcode::flat_atomic_umin_x2;
         break;
      case nir_atomic_op_imax:
         op32 = global ? aco_opcode::global_atomic_smax : aco_opcode::flat_atomic_smax;
         op64 = global ? aco_opcode::global_atomic_smax_x2 : aco_opcode::flat_atomic_smax_x2;
         break;
      case nir_atomic_op_umax:
         op32 = global ? aco_opcode::global_atomic_umax : aco_opcode::flat_atomic_umax;
         op64 = global ? aco_opcode::global_atomic_umax_x2 : aco_opcode::flat_atomic_umax_x2;
         break;
      case nir_atomic_op_iand:
         op32 = global ? aco_opcode::global_atomic_and : aco_opcode::flat_atomic_and;
         op64 = global ? aco_opcode::global_atomic_and_x2 : aco_opcode::flat_atomic_and_x2;
         break;
      case nir_atomic_op_ior:
         op32 = global ? aco_opcode::global_atomic_or : aco_opcode::flat_atomic_or;
         op64 = global ? aco_opcode::global_atomic_or_x2 : aco_opcode::flat_atomic_or_x2;
         break;
      case nir_atomic_op_ixor:
         op32 = global ? aco_opcode::global_atomic_xor : aco_opcode::flat_atomic_xor;
         op64 = global ? aco_opcode::global_atomic_xor_x2 : aco_opcode::flat_atomic_xor_x2;
         break;
      case nir_atomic_op_xchg:
         op32 = global ? aco_opcode::global_atomic_swap : aco_opcode::flat_atomic_swap;
         op64 = global ? aco_opcode::global_atomic_swap_x2 : aco_opcode::flat_atomic_swap_x2;
         break;
      case nir_atomic_op_cmpxchg:
         op32 = global ? aco_opcode::global_atomic_cmpswap : aco_opcode::flat_atomic_cmpswap;
         op64 = global ? aco_opcode::global_atomic_cmpswap_x2 : aco_opcode::flat_atomic_cmpswap_x2;
         break;
      case nir_atomic_op_fadd:
         op32 = global ? aco_opcode::global_atomic_add_f32 : aco_opcode::flat_atomic_add_f32;
         op64 = aco_opcode::num_opcodes;
         break;
      case nir_atomic_op_fmin:
         op32 = global ? aco_opcode::global_atomic_fmin : aco_opcode::flat_atomic_fmin;
         op64 = global ? aco_opcode::global_atomic_fmin_x2 : aco_opcode::flat_atomic_fmin_x2;
         break;
      case nir_atomic_op_fmax:
         op32 = global ? aco_opcode::global_atomic_fmax : aco_opcode::flat_atomic_fmax;
         op64 = global ? aco_opcode::global_atomic_fmax_x2 : aco_opcode::flat_atomic_fmax_x2;
         break;
      case nir_atomic_op_ordered_add_gfx12_amd:
         assert(ctx->options->gfx_level >= GFX12 && instr->def.bit_size == 64);
         op32 = aco_opcode::num_opcodes;
         op64 = aco_opcode::global_atomic_ordered_add_b64;
         break;
      default: UNREACHABLE("unsupported atomic operation");
      }

      aco_opcode op = instr->def.bit_size == 32 ? op32 : op64;
      aco_ptr<Instruction> flat{create_instruction(op, format, 5, return_previous ? 1 : 0)};
      if (addr.regClass() == s2) {
         assert(global && offset.id() && offset.type() == RegType::vgpr);
         flat->operands[0] = Operand(offset);
         flat->operands[1] = Operand(addr);
      } else {
         assert(addr.type() == RegType::vgpr && !offset.id());
         flat->operands[0] = Operand(addr);
         flat->operands[1] = Operand(s1);
      }
      flat->operands[2] = Operand(data);
      if (return_previous)
         flat->definitions[0] = Definition(dst);
      flat->flatlike().cache = get_atomic_cache_flags(ctx, return_previous);
      assert(global || !const_offset);
      flat->flatlike().offset = const_offset;
      flat->flatlike().sync = get_memory_sync_info(instr, storage_buffer, semantic_atomicrmw);
      init_disable_wqm(bld, flat->flatlike(), true);
      ctx->block->instructions.emplace_back(std::move(flat));
   } else {
      assert(ctx->options->gfx_level == GFX6 || addr.type() != RegType::vgpr);

      UNUSED aco_opcode image_op;
      translate_buffer_image_atomic_op(nir_op, &op32, &op64, &image_op);

      Temp rsrc = get_mubuf_global_rsrc(bld, addr);

      aco_opcode op = instr->def.bit_size == 32 ? op32 : op64;

      aco_ptr<Instruction> mubuf{create_instruction(op, Format::MUBUF, 6, return_previous ? 1 : 0)};
      mubuf->operands[0] = Operand(rsrc);
      if (addr.type() == RegType::vgpr)
         mubuf->operands[1] = Operand(addr);
      else if (offset.type() == RegType::vgpr)
         mubuf->operands[1] = Operand(offset);
      else
         mubuf->operands[1] = Operand(v1);
      mubuf->operands[2] = offset.type() == RegType::sgpr ? Operand(offset) : Operand::c32(0);
      mubuf->operands[3] = Operand(data);
      Definition def =
         return_previous ? (cmpswap ? bld.def(data.regClass()) : Definition(dst)) : Definition();
      if (return_previous)
         mubuf->definitions[0] = def;
      mubuf->mubuf().offen = offset.type() == RegType::vgpr;
      mubuf->mubuf().cache = get_atomic_cache_flags(ctx, return_previous);
      mubuf->mubuf().offset = const_offset;
      mubuf->mubuf().addr64 = addr.type() == RegType::vgpr;
      mubuf->mubuf().sync = get_memory_sync_info(instr, storage_buffer, semantic_atomicrmw);
      init_disable_wqm(bld, mubuf->mubuf(), true);
      ctx->block->instructions.emplace_back(std::move(mubuf));
      if (return_previous && cmpswap)
         bld.pseudo(aco_opcode::p_extract_vector, Definition(dst), def.getTemp(), Operand::zero());
   }
}

unsigned
aco_storage_mode_from_nir_mem_mode(unsigned mem_mode)
{
   unsigned storage = storage_none;

   if (mem_mode & nir_var_shader_out)
      storage |= storage_vmem_output;
   if ((mem_mode & nir_var_mem_ssbo) || (mem_mode & nir_var_mem_global))
      storage |= storage_buffer;
   if (mem_mode & nir_var_mem_task_payload)
      storage |= storage_task_payload;
   if (mem_mode & nir_var_mem_shared)
      storage |= storage_shared;
   if (mem_mode & nir_var_image)
      storage |= storage_image;

   return storage;
}

void
visit_load_buffer(isel_context* ctx, nir_intrinsic_instr* intrin)
{
   Builder bld(ctx->program, ctx->block);

   /* Swizzled buffer addressing seems to be broken on GFX11 without the idxen bit. */
   bool swizzled = nir_intrinsic_access(intrin) & ACCESS_IS_SWIZZLED_AMD;
   bool idxen = (swizzled && ctx->program->gfx_level >= GFX11) ||
                !nir_src_is_const(intrin->src[3]) || nir_src_as_uint(intrin->src[3]);
   bool v_offset_zero = nir_src_is_const(intrin->src[1]) && !nir_src_as_uint(intrin->src[1]);
   bool s_offset_zero = nir_src_is_const(intrin->src[2]) && !nir_src_as_uint(intrin->src[2]);

   Temp dst = get_ssa_temp(ctx, &intrin->def);
   Temp descriptor = bld.as_uniform(get_ssa_temp(ctx, intrin->src[0].ssa));
   Temp v_offset =
      v_offset_zero ? Temp(0, v1) : as_vgpr(ctx, get_ssa_temp(ctx, intrin->src[1].ssa));
   Temp s_offset =
      s_offset_zero ? Temp(0, s1) : bld.as_uniform(get_ssa_temp(ctx, intrin->src[2].ssa));
   Temp idx = idxen ? as_vgpr(ctx, get_ssa_temp(ctx, intrin->src[3].ssa)) : Temp();

   ac_hw_cache_flags cache =
      get_cache_flags(ctx, nir_intrinsic_access(intrin), ac_access_type_load);

   unsigned const_offset = nir_intrinsic_base(intrin);
   unsigned elem_size_bytes = intrin->def.bit_size / 8u;
   unsigned num_components = intrin->def.num_components;

   nir_variable_mode mem_mode = nir_intrinsic_memory_modes(intrin);
   memory_sync_info sync(aco_storage_mode_from_nir_mem_mode(mem_mode));

   const unsigned align_mul = nir_intrinsic_align_mul(intrin);
   const unsigned align_offset = nir_intrinsic_align_offset(intrin);

   LoadEmitInfo info = {Operand(v_offset), dst, num_components, elem_size_bytes, descriptor};
   info.idx = idx;
   info.cache = cache;
   info.soffset = s_offset;
   info.const_offset = const_offset;
   info.sync = sync;

   if (intrin->intrinsic == nir_intrinsic_load_typed_buffer_amd) {
      const pipe_format format = nir_intrinsic_format(intrin);
      const struct ac_vtx_format_info* vtx_info =
         ac_get_vtx_format_info(ctx->program->gfx_level, ctx->program->family, format);
      const struct util_format_description* f = util_format_description(format);

      /* Avoid splitting:
       * - non-array formats because that would result in incorrect code
       * - when element size is same as component size (to reduce instruction count)
       */
      const bool can_split = f->is_array && elem_size_bytes != vtx_info->chan_byte_size;

      info.align_mul = align_mul;
      info.align_offset = align_offset;
      info.format = format;
      info.component_stride = can_split ? vtx_info->chan_byte_size : 0;
      info.split_by_component_stride = false;

      EmitLoadParameters params = mtbuf_load_params;
      params.max_const_offset = ctx->program->dev.buf_offset_max;
      emit_load(ctx, bld, info, params);
   } else {
      assert(intrin->intrinsic == nir_intrinsic_load_buffer_amd);

      if (nir_intrinsic_access(intrin) & ACCESS_USES_FORMAT_AMD) {
         assert(!swizzled);

         EmitLoadParameters params = mubuf_load_format_params;
         params.max_const_offset = ctx->program->dev.buf_offset_max;
         emit_load(ctx, bld, info, params);
      } else {
         const unsigned swizzle_element_size =
            swizzled ? (ctx->program->gfx_level <= GFX8 ? 4 : 16) : 0;

         info.component_stride = swizzle_element_size;
         info.swizzle_component_size = swizzle_element_size ? 4 : 0;
         info.align_mul = align_mul;
         info.align_offset = align_offset;

         EmitLoadParameters params = mubuf_load_params;
         params.max_const_offset = ctx->program->dev.buf_offset_max;
         emit_load(ctx, bld, info, params);
      }
   }
}

void
visit_store_buffer(isel_context* ctx, nir_intrinsic_instr* intrin)
{
   Builder bld(ctx->program, ctx->block);

   /* Swizzled buffer addressing seems to be broken on GFX11 without the idxen bit. */
   bool swizzled = nir_intrinsic_access(intrin) & ACCESS_IS_SWIZZLED_AMD;
   bool idxen = (swizzled && ctx->program->gfx_level >= GFX11) ||
                !nir_src_is_const(intrin->src[4]) || nir_src_as_uint(intrin->src[4]);
   bool offen = !nir_src_is_const(intrin->src[2]) || nir_src_as_uint(intrin->src[2]);

   Temp store_src = get_ssa_temp(ctx, intrin->src[0].ssa);
   Temp descriptor = bld.as_uniform(get_ssa_temp(ctx, intrin->src[1].ssa));
   Temp v_offset = offen ? as_vgpr(ctx, get_ssa_temp(ctx, intrin->src[2].ssa)) : Temp();
   Temp s_offset = bld.as_uniform(get_ssa_temp(ctx, intrin->src[3].ssa));
   Temp idx = idxen ? as_vgpr(ctx, get_ssa_temp(ctx, intrin->src[4].ssa)) : Temp();

   unsigned elem_size_bytes = intrin->src[0].ssa->bit_size / 8u;
   assert(elem_size_bytes == 1 || elem_size_bytes == 2 || elem_size_bytes == 4 ||
          elem_size_bytes == 8);

   unsigned write_mask = nir_intrinsic_write_mask(intrin);
   write_mask = util_widen_mask(write_mask, elem_size_bytes);

   nir_variable_mode mem_mode = nir_intrinsic_memory_modes(intrin);
   /* GS outputs are only written once. */
   const bool written_once =
      mem_mode == nir_var_shader_out && ctx->shader->info.stage == MESA_SHADER_GEOMETRY;
   memory_sync_info sync(aco_storage_mode_from_nir_mem_mode(mem_mode),
                         written_once ? semantic_can_reorder : semantic_none);

   unsigned write_count = 0;
   Temp write_datas[32];
   unsigned offsets[32];
   split_buffer_store(ctx, intrin, false, RegType::vgpr, store_src, write_mask,
                      swizzled && ctx->program->gfx_level <= GFX8 ? 4 : 16, &write_count,
                      write_datas, offsets);

   for (unsigned i = 0; i < write_count; i++) {
      aco_opcode op = get_buffer_store_op(write_datas[i].bytes());
      Temp write_voffset = v_offset;
      unsigned const_offset = resolve_excess_vmem_const_offset(
         bld, write_voffset, offsets[i] + nir_intrinsic_base(intrin));

      /* write_voffset may be updated in resolve_excess_vmem_const_offset(). */
      offen = write_voffset.id();

      Operand vaddr_op(v1);
      if (offen && idxen)
         vaddr_op = bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), idx, write_voffset);
      else if (offen)
         vaddr_op = Operand(write_voffset);
      else if (idxen)
         vaddr_op = Operand(idx);

      unsigned access = nir_intrinsic_access(intrin);
      enum ac_access_type type = ac_access_type_store;
      if (write_datas[i].bytes() < 4)
         type = ac_access_type_store_subdword;
      ac_hw_cache_flags cache = get_cache_flags(ctx, access, type);

      Instruction* mubuf = bld.mubuf(op, Operand(descriptor), vaddr_op, s_offset,
                                     Operand(write_datas[i]), const_offset, offen, idxen,
                                     /* addr64 */ false, /* disable_wqm */ false, cache)
                              .instr;
      mubuf->mubuf().sync = sync;
   }
}

sync_scope
translate_nir_scope(mesa_scope scope)
{
   switch (scope) {
   case SCOPE_NONE:
   case SCOPE_INVOCATION: return scope_invocation;
   case SCOPE_SUBGROUP: return scope_subgroup;
   case SCOPE_WORKGROUP: return scope_workgroup;
   case SCOPE_QUEUE_FAMILY: return scope_queuefamily;
   case SCOPE_DEVICE: return scope_device;
   case SCOPE_SHADER_CALL: return scope_invocation;
   }
   UNREACHABLE("invalid scope");
}

void
emit_barrier(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);

   unsigned storage_allowed = storage_buffer | storage_image;
   unsigned semantics = 0;
   sync_scope mem_scope = translate_nir_scope(nir_intrinsic_memory_scope(instr));
   sync_scope exec_scope = translate_nir_scope(nir_intrinsic_execution_scope(instr));

   /* We use shared storage for the following:
    * - compute shaders expose it in their API
    * - when tessellation is used, TCS and VS I/O is lowered to shared memory
    * - when GS is used on GFX9+, VS->GS and TES->GS I/O is lowered to shared memory
    * - additionally, when NGG is used on GFX10+, shared memory is used for certain features
    */
   bool shared_storage_used =
      ctx->stage.hw == AC_HW_COMPUTE_SHADER || ctx->stage.hw == AC_HW_LOCAL_SHADER ||
      ctx->stage.hw == AC_HW_HULL_SHADER ||
      (ctx->stage.hw == AC_HW_LEGACY_GEOMETRY_SHADER && ctx->program->gfx_level >= GFX9) ||
      ctx->stage.hw == AC_HW_NEXT_GEN_GEOMETRY_SHADER;

   if (shared_storage_used)
      storage_allowed |= storage_shared;

   /* Task payload: Task Shader output, Mesh Shader input */
   if (ctx->stage.has(SWStage::MS) || ctx->stage.has(SWStage::TS))
      storage_allowed |= storage_task_payload;

   /* Allow VMEM output for all stages that can have outputs. */
   if ((ctx->stage.hw != AC_HW_COMPUTE_SHADER && ctx->stage.hw != AC_HW_PIXEL_SHADER) ||
       ctx->stage.has(SWStage::TS))
      storage_allowed |= storage_vmem_output;

   /* Workgroup barriers can hang merged shaders that can potentially have 0 threads in either half.
    * They are allowed in CS, TCS, and in any NGG shader.
    */
   ASSERTED bool workgroup_scope_allowed = ctx->stage.hw == AC_HW_COMPUTE_SHADER ||
                                           ctx->stage.hw == AC_HW_HULL_SHADER ||
                                           ctx->stage.hw == AC_HW_NEXT_GEN_GEOMETRY_SHADER;

   unsigned nir_storage = nir_intrinsic_memory_modes(instr);
   unsigned storage = aco_storage_mode_from_nir_mem_mode(nir_storage);
   storage &= storage_allowed;

   unsigned nir_semantics = nir_intrinsic_memory_semantics(instr);
   if (nir_semantics & NIR_MEMORY_ACQUIRE)
      semantics |= semantic_acquire;
   if (nir_semantics & NIR_MEMORY_RELEASE)
      semantics |= semantic_release;

   assert(!(nir_semantics & (NIR_MEMORY_MAKE_AVAILABLE | NIR_MEMORY_MAKE_VISIBLE)));
   assert(exec_scope != scope_workgroup || workgroup_scope_allowed);

   bld.barrier(aco_opcode::p_barrier,
               memory_sync_info((storage_class)storage, (memory_semantics)semantics, mem_scope),
               exec_scope);
}

/* The two 32 wide halves of a gfx10+ wave64 LDS instruction might be executed interleaved
 * with LDS instructions from the other CU in WGP mode.
 */
bool
lds_potential_non_atomic_split(isel_context* ctx, unsigned access)
{
   return ctx->options->gfx_level >= GFX10 && ctx->program->wave_size == 64 &&
          ctx->program->workgroup_size > 64 && ctx->program->wgp_mode &&
          ((access & ACCESS_ATOMIC) || !ctx->shader->info.assume_no_data_races);
}

void
visit_load_shared(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Temp dst = get_ssa_temp(ctx, &instr->def);
   Temp address = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[0].ssa));
   Builder bld(ctx->program, ctx->block);

   unsigned elem_size_bytes = instr->def.bit_size / 8;
   unsigned num_components = instr->def.num_components;
   unsigned bytes = elem_size_bytes * num_components;
   unsigned align = nir_intrinsic_align_mul(instr) ? nir_intrinsic_align(instr) : elem_size_bytes;
   assert(bytes == 12 ? align % 16 == 0 : align % bytes == 0);

   Operand m = load_lds_size_m0(bld);
   aco_opcode op;

   switch (bytes) {
   case 16: op = aco_opcode::ds_read_b128; break;
   case 12: op = aco_opcode::ds_read_b96; break;
   case 8: op = aco_opcode::ds_read_b64; break;
   case 4: op = aco_opcode::ds_read_b32; break;
   case 2:
      op = bld.program->gfx_level >= GFX9 ? aco_opcode::ds_read_u16_d16 : aco_opcode::ds_read_u16;
      break;
   case 1:
      op = bld.program->gfx_level >= GFX9 ? aco_opcode::ds_read_u8_d16 : aco_opcode::ds_read_u8;
      break;
   default: UNREACHABLE("Unsupported load_shared size");
   }

   unsigned const_offset = nir_intrinsic_base(instr);
   assert(const_offset <= UINT16_MAX);

   Definition def = dst.regClass().type() == RegType::sgpr
                       ? bld.def(RegClass::get(RegType::vgpr, bytes))
                       : Definition(dst);
   Instruction* ds = bld.ds(op, def, address, m, const_offset);
   ds->ds().sync = get_memory_sync_info(instr, storage_shared, 0);

   if (m.isUndefined())
      ds->operands.pop_back();

   if (def.getTemp() != dst) {
      /* Use v_readfirstlane instead of p_as_uniform in order to avoid copy-propagation of
       * potentially divergent value.
       */
      bool readfirstlane_for_uniform =
         lds_potential_non_atomic_split(ctx, nir_intrinsic_access(instr));
      emit_vector_as_uniform(ctx, def.getTemp(), dst, readfirstlane_for_uniform);
   }

   emit_split_vector(ctx, dst, instr->def.num_components);
}

void
visit_store_shared(isel_context* ctx, nir_intrinsic_instr* instr)
{
   ASSERTED unsigned writemask = nir_intrinsic_write_mask(instr);
   assert(writemask == BITFIELD_MASK(instr->src[0].ssa->num_components));
   Temp data = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[0].ssa));
   Temp address = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[1].ssa));

   unsigned elem_size_bytes = instr->src[0].ssa->bit_size / 8;
   unsigned num_components = instr->src[0].ssa->num_components;
   unsigned bytes = elem_size_bytes * num_components;
   assert(bytes <= 8 || ctx->program->gfx_level >= GFX7);
   ASSERTED unsigned align =
      nir_intrinsic_align_mul(instr) ? nir_intrinsic_align(instr) : elem_size_bytes;
   assert(align % bytes == 0 || (align % 16 == 0 && bytes == 12));

   Builder bld(ctx->program, ctx->block);
   Operand m = load_lds_size_m0(bld);
   aco_opcode op;
   switch (bytes) {
   case 16: op = aco_opcode::ds_write_b128; break;
   case 12: op = aco_opcode::ds_write_b96; break;
   case 8: op = aco_opcode::ds_write_b64; break;
   case 4: op = aco_opcode::ds_write_b32; break;
   case 2: op = aco_opcode::ds_write_b16; break;
   case 1: op = aco_opcode::ds_write_b8; break;
   default: UNREACHABLE("Unsupported load_shared size");
   }

   unsigned const_offset = nir_intrinsic_base(instr);
   assert(const_offset <= UINT16_MAX);

   Instruction* ds = bld.ds(op, address, data, m, const_offset);
   ds->ds().sync = get_memory_sync_info(instr, storage_shared, 0);

   if (m.isUndefined())
      ds->operands.pop_back();
}

void
visit_shared_atomic(isel_context* ctx, nir_intrinsic_instr* instr)
{
   unsigned offset = nir_intrinsic_base(instr);
   Builder bld(ctx->program, ctx->block);
   Operand m = load_lds_size_m0(bld);
   Temp data = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[1].ssa));
   Temp address = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[0].ssa));

   unsigned num_operands = 3;
   aco_opcode op32, op64, op32_rtn, op64_rtn;
   switch (nir_intrinsic_atomic_op(instr)) {
   case nir_atomic_op_iadd:
      op32 = aco_opcode::ds_add_u32;
      op64 = aco_opcode::ds_add_u64;
      op32_rtn = aco_opcode::ds_add_rtn_u32;
      op64_rtn = aco_opcode::ds_add_rtn_u64;
      break;
   case nir_atomic_op_isub:
      op32 = aco_opcode::ds_sub_u32;
      op64 = aco_opcode::ds_sub_u64;
      op32_rtn = aco_opcode::ds_sub_rtn_u32;
      op64_rtn = aco_opcode::ds_sub_rtn_u64;
      break;
   case nir_atomic_op_imin:
      op32 = aco_opcode::ds_min_i32;
      op64 = aco_opcode::ds_min_i64;
      op32_rtn = aco_opcode::ds_min_rtn_i32;
      op64_rtn = aco_opcode::ds_min_rtn_i64;
      break;
   case nir_atomic_op_umin:
      op32 = aco_opcode::ds_min_u32;
      op64 = aco_opcode::ds_min_u64;
      op32_rtn = aco_opcode::ds_min_rtn_u32;
      op64_rtn = aco_opcode::ds_min_rtn_u64;
      break;
   case nir_atomic_op_imax:
      op32 = aco_opcode::ds_max_i32;
      op64 = aco_opcode::ds_max_i64;
      op32_rtn = aco_opcode::ds_max_rtn_i32;
      op64_rtn = aco_opcode::ds_max_rtn_i64;
      break;
   case nir_atomic_op_umax:
      op32 = aco_opcode::ds_max_u32;
      op64 = aco_opcode::ds_max_u64;
      op32_rtn = aco_opcode::ds_max_rtn_u32;
      op64_rtn = aco_opcode::ds_max_rtn_u64;
      break;
   case nir_atomic_op_iand:
      op32 = aco_opcode::ds_and_b32;
      op64 = aco_opcode::ds_and_b64;
      op32_rtn = aco_opcode::ds_and_rtn_b32;
      op64_rtn = aco_opcode::ds_and_rtn_b64;
      break;
   case nir_atomic_op_ior:
      op32 = aco_opcode::ds_or_b32;
      op64 = aco_opcode::ds_or_b64;
      op32_rtn = aco_opcode::ds_or_rtn_b32;
      op64_rtn = aco_opcode::ds_or_rtn_b64;
      break;
   case nir_atomic_op_ixor:
      op32 = aco_opcode::ds_xor_b32;
      op64 = aco_opcode::ds_xor_b64;
      op32_rtn = aco_opcode::ds_xor_rtn_b32;
      op64_rtn = aco_opcode::ds_xor_rtn_b64;
      break;
   case nir_atomic_op_xchg:
      op32 = aco_opcode::ds_write_b32;
      op64 = aco_opcode::ds_write_b64;
      op32_rtn = aco_opcode::ds_wrxchg_rtn_b32;
      op64_rtn = aco_opcode::ds_wrxchg_rtn_b64;
      break;
   case nir_atomic_op_cmpxchg:
      op32 = aco_opcode::ds_cmpst_b32;
      op64 = aco_opcode::ds_cmpst_b64;
      op32_rtn = aco_opcode::ds_cmpst_rtn_b32;
      op64_rtn = aco_opcode::ds_cmpst_rtn_b64;
      num_operands = 4;
      break;
   case nir_atomic_op_fadd:
      op32 = aco_opcode::ds_add_f32;
      op32_rtn = aco_opcode::ds_add_rtn_f32;
      op64 = aco_opcode::num_opcodes;
      op64_rtn = aco_opcode::num_opcodes;
      break;
   case nir_atomic_op_fmin:
      op32 = aco_opcode::ds_min_f32;
      op32_rtn = aco_opcode::ds_min_rtn_f32;
      op64 = aco_opcode::ds_min_f64;
      op64_rtn = aco_opcode::ds_min_rtn_f64;
      break;
   case nir_atomic_op_fmax:
      op32 = aco_opcode::ds_max_f32;
      op32_rtn = aco_opcode::ds_max_rtn_f32;
      op64 = aco_opcode::ds_max_f64;
      op64_rtn = aco_opcode::ds_max_rtn_f64;
      break;
   default: UNREACHABLE("Unhandled shared atomic intrinsic");
   }

   bool return_previous = !nir_def_is_unused(&instr->def);

   aco_opcode op;
   if (data.size() == 1) {
      assert(instr->def.bit_size == 32);
      op = return_previous ? op32_rtn : op32;
   } else {
      assert(instr->def.bit_size == 64);
      op = return_previous ? op64_rtn : op64;
   }

   if (offset > 65535) {
      address = bld.vadd32(bld.def(v1), Operand::c32(offset), address);
      offset = 0;
   }

   aco_ptr<Instruction> ds;
   ds.reset(create_instruction(op, Format::DS, num_operands, return_previous ? 1 : 0));
   ds->operands[0] = Operand(address);
   ds->operands[1] = Operand(data);
   if (num_operands == 4) {
      Temp data2 = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[2].ssa));
      ds->operands[2] = Operand(data2);
      if (bld.program->gfx_level >= GFX11)
         std::swap(ds->operands[1], ds->operands[2]);
   }
   ds->operands[num_operands - 1] = m;
   ds->ds().offset0 = offset;
   if (return_previous)
      ds->definitions[0] = Definition(get_ssa_temp(ctx, &instr->def));
   ds->ds().sync = memory_sync_info(storage_shared, semantic_atomicrmw);

   if (m.isUndefined())
      ds->operands.pop_back();

   ctx->block->instructions.emplace_back(std::move(ds));
}

void
visit_shared_append(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   unsigned address = nir_intrinsic_base(instr);
   assert(address <= 65535 && (address % 4 == 0));

   aco_opcode op;
   switch (instr->intrinsic) {
   case nir_intrinsic_shared_append_amd: op = aco_opcode::ds_append; break;
   case nir_intrinsic_shared_consume_amd: op = aco_opcode::ds_consume; break;
   default: UNREACHABLE("not shared_append/consume");
   }

   Temp tmp = bld.tmp(v1);
   Instruction* ds;
   Operand m = load_lds_size_m0(bld);
   if (m.isUndefined())
      ds = bld.ds(op, Definition(tmp), address);
   else
      ds = bld.ds(op, Definition(tmp), m, address);
   ds->ds().sync = memory_sync_info(storage_shared, semantic_atomicrmw);

   /* If there is a write to the same LDS address between the split halves, only the second half
    * will read the correct result.
    */
   if (lds_potential_non_atomic_split(ctx, ACCESS_ATOMIC)) {
      Temp last_lane = bld.sop1(aco_opcode::s_flbit_i32_b64, bld.def(s1), Operand(exec, s2));
      last_lane = bld.sop2(aco_opcode::s_sub_u32, bld.def(s1), bld.def(s1, scc), Operand::c32(63),
                           last_lane);
      bld.readlane(Definition(get_ssa_temp(ctx, &instr->def)), tmp, last_lane);
   } else {
      bld.pseudo(aco_opcode::p_as_uniform, Definition(get_ssa_temp(ctx, &instr->def)), tmp);
   }
}

void
visit_access_shared2_amd(isel_context* ctx, nir_intrinsic_instr* instr)
{
   bool is_store = instr->intrinsic == nir_intrinsic_store_shared2_amd;
   Temp address = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[is_store].ssa));
   Builder bld(ctx->program, ctx->block);

   bool is64bit = (is_store ? instr->src[0].ssa->bit_size : instr->def.bit_size) == 64;
   uint8_t offset0 = nir_intrinsic_offset0(instr);
   uint8_t offset1 = nir_intrinsic_offset1(instr);
   bool st64 = nir_intrinsic_st64(instr);

   Operand m = load_lds_size_m0(bld);
   Instruction* ds;
   if (is_store) {
      aco_opcode op = st64
                         ? (is64bit ? aco_opcode::ds_write2st64_b64 : aco_opcode::ds_write2st64_b32)
                         : (is64bit ? aco_opcode::ds_write2_b64 : aco_opcode::ds_write2_b32);
      Temp data = get_ssa_temp(ctx, instr->src[0].ssa);
      RegClass comp_rc = is64bit ? v2 : v1;
      Temp data0 = emit_extract_vector(ctx, data, 0, comp_rc);
      Temp data1 = emit_extract_vector(ctx, data, 1, comp_rc);
      ds = bld.ds(op, address, data0, data1, m, offset0, offset1);
   } else {
      Temp dst = get_ssa_temp(ctx, &instr->def);
      Definition tmp_dst(dst.type() == RegType::vgpr ? dst : bld.tmp(is64bit ? v4 : v2));
      aco_opcode op = st64 ? (is64bit ? aco_opcode::ds_read2st64_b64 : aco_opcode::ds_read2st64_b32)
                           : (is64bit ? aco_opcode::ds_read2_b64 : aco_opcode::ds_read2_b32);
      ds = bld.ds(op, tmp_dst, address, m, offset0, offset1);
   }
   ds->ds().sync = get_memory_sync_info(instr, storage_shared, 0);
   if (m.isUndefined())
      ds->operands.pop_back();

   if (!is_store) {
      Temp dst = get_ssa_temp(ctx, &instr->def);
      if (dst.type() == RegType::sgpr) {
         /* Similar to load_shared. */
         bool readfirstlane_for_uniform =
            lds_potential_non_atomic_split(ctx, nir_intrinsic_access(instr));

         emit_split_vector(ctx, ds->definitions[0].getTemp(), dst.size());
         Temp comp[4];
         /* Use scalar v_readfirstlane_b32 for better 32-bit copy propagation */
         for (unsigned i = 0; i < dst.size(); i++) {
            Temp val = emit_extract_vector(ctx, ds->definitions[0].getTemp(), i, v1);
            comp[i] = emit_vector_as_uniform(ctx, val, bld.tmp(s1), readfirstlane_for_uniform);
         }
         if (is64bit) {
            Temp comp0 = bld.pseudo(aco_opcode::p_create_vector, bld.def(s2), comp[0], comp[1]);
            Temp comp1 = bld.pseudo(aco_opcode::p_create_vector, bld.def(s2), comp[2], comp[3]);
            ctx->allocated_vec[comp0.id()] = {comp[0], comp[1]};
            ctx->allocated_vec[comp1.id()] = {comp[2], comp[3]};
            bld.pseudo(aco_opcode::p_create_vector, Definition(dst), comp0, comp1);
            ctx->allocated_vec[dst.id()] = {comp0, comp1};
         } else {
            bld.pseudo(aco_opcode::p_create_vector, Definition(dst), comp[0], comp[1]);
         }
      }

      emit_split_vector(ctx, dst, 2);
   }
}

void
visit_load_scratch(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   Temp dst = get_ssa_temp(ctx, &instr->def);

   LoadEmitInfo info = {Operand(v1), dst, instr->def.num_components, instr->def.bit_size / 8u};
   info.align_mul = nir_intrinsic_align_mul(instr);
   info.align_offset = nir_intrinsic_align_offset(instr);
   info.cache = get_cache_flags(ctx, ACCESS_IS_SWIZZLED_AMD, ac_access_type_load);
   info.swizzle_component_size = ctx->program->gfx_level <= GFX8 ? 4 : 0;
   info.sync = memory_sync_info(storage_scratch, semantic_private);
   info.disable_wqm = nir_intrinsic_access(instr) & ACCESS_SKIP_HELPERS;
   if (ctx->program->gfx_level >= GFX9) {
      if (nir_src_is_const(instr->src[0])) {
         info.const_offset = nir_src_as_uint(instr->src[0]);
         if (ctx->program->stack_ptr.id())
            info.offset = Operand(ctx->program->stack_ptr);
         else
            info.offset = Operand::zero(4);
      } else {
         info.offset = Operand(get_ssa_temp(ctx, instr->src[0].ssa));
         if (ctx->program->stack_ptr.id()) {
            if (info.offset.regClass().type() == RegType::sgpr) {
               info.offset = bld.sop2(aco_opcode::s_add_u32, bld.def(s1), bld.def(s1, scc),
                                      ctx->program->stack_ptr, info.offset);
            } else {
               info.offset = bld.vadd32(bld.def(v1), ctx->program->stack_ptr, info.offset);
            }
         }
      }
      EmitLoadParameters params = scratch_flat_load_params;
      params.max_const_offset = ctx->program->dev.scratch_global_offset_max;
      emit_load(ctx, bld, info, params);
   } else {
      info.resource = load_scratch_resource(
         ctx->program, bld, ctx->program->private_segment_buffers.size() - 1, false);
      info.offset = Operand(as_vgpr(ctx, get_ssa_temp(ctx, instr->src[0].ssa)));
      if (!ctx->program->scratch_offsets.empty())
         info.soffset = ctx->program->scratch_offsets.back();
      emit_load(ctx, bld, info, scratch_mubuf_load_params);
   }
}

void
visit_store_scratch(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   Temp data = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[0].ssa));
   Temp offset = get_ssa_temp(ctx, instr->src[1].ssa);

   unsigned elem_size_bytes = instr->src[0].ssa->bit_size / 8;
   unsigned writemask = util_widen_mask(nir_intrinsic_write_mask(instr), elem_size_bytes);

   unsigned write_count = 0;
   Temp write_datas[32];
   unsigned offsets[32];
   unsigned swizzle_component_size = ctx->program->gfx_level <= GFX8 ? 4 : 16;
   split_buffer_store(ctx, instr, false, RegType::vgpr, data, writemask, swizzle_component_size,
                      &write_count, write_datas, offsets);

   if (ctx->program->gfx_level >= GFX9) {
      uint32_t max = ctx->program->dev.scratch_global_offset_max + 1;
      offset = nir_src_is_const(instr->src[1]) ? Temp(0, s1) : offset;
      uint32_t base_const_offset =
         nir_src_is_const(instr->src[1]) ? nir_src_as_uint(instr->src[1]) : 0;

      if (ctx->program->stack_ptr.id()) {
         if (offset.id() == 0)
            offset = ctx->program->stack_ptr;
         else if (offset.type() == RegType::sgpr)
            offset = bld.sop2(aco_opcode::s_add_u32, bld.def(s1), bld.def(s1, scc),
                              Operand(ctx->program->stack_ptr), Operand(offset));
         else
            offset = bld.vadd32(bld.def(v1), Operand(ctx->program->stack_ptr), Operand(offset));
      }

      for (unsigned i = 0; i < write_count; i++) {
         aco_opcode op;
         switch (write_datas[i].bytes()) {
         case 1: op = aco_opcode::scratch_store_byte; break;
         case 2: op = aco_opcode::scratch_store_short; break;
         case 4: op = aco_opcode::scratch_store_dword; break;
         case 8: op = aco_opcode::scratch_store_dwordx2; break;
         case 12: op = aco_opcode::scratch_store_dwordx3; break;
         case 16: op = aco_opcode::scratch_store_dwordx4; break;
         default: UNREACHABLE("Unexpected store size");
         }

         uint32_t const_offset = base_const_offset + offsets[i];

         Operand addr = offset.regClass() == s1 ? Operand(v1) : Operand(offset);
         Operand saddr = offset.regClass() == s1 ? Operand(offset) : Operand(s1);
         if (offset.id() && const_offset >= max) {
            assert(offset == ctx->program->stack_ptr);
            saddr =
               bld.sop2(aco_opcode::s_add_u32, bld.def(s1), bld.def(s1, scc),
                        ctx->program->stack_ptr, Operand::c32(ROUND_DOWN_TO(const_offset, max)));
         } else if (offset.id() == 0) {
            saddr = bld.copy(bld.def(s1), Operand::c32(ROUND_DOWN_TO(const_offset, max)));
         }

         bld.scratch(op, addr, saddr, write_datas[i], const_offset % max,
                     memory_sync_info(storage_scratch, semantic_private));
      }
   } else {
      Temp rsrc = load_scratch_resource(ctx->program, bld,
                                        ctx->program->private_segment_buffers.size() - 1, false);
      offset = as_vgpr(ctx, offset);
      for (unsigned i = 0; i < write_count; i++) {
         aco_opcode op = get_buffer_store_op(write_datas[i].bytes());
         Instruction* mubuf = bld.mubuf(op, rsrc, offset, ctx->program->scratch_offsets.back(),
                                        write_datas[i], offsets[i], true);
         mubuf->mubuf().sync = memory_sync_info(storage_scratch, semantic_private);
         enum ac_access_type type =
            write_datas[i].bytes() < 4 ? ac_access_type_store_subdword : ac_access_type_store;
         mubuf->mubuf().cache = get_cache_flags(ctx, ACCESS_IS_SWIZZLED_AMD, type);
      }
   }
}

ReduceOp
get_reduce_op(nir_op op, unsigned bit_size)
{
   switch (op) {
#define CASEI(name)                                                                                \
   case nir_op_##name:                                                                             \
      return (bit_size == 32)   ? name##32                                                         \
             : (bit_size == 16) ? name##16                                                         \
             : (bit_size == 8)  ? name##8                                                          \
                                : name##64;
#define CASEF(name)                                                                                \
   case nir_op_##name: return (bit_size == 32) ? name##32 : (bit_size == 16) ? name##16 : name##64;
      CASEI(iadd)
      CASEI(imul)
      CASEI(imin)
      CASEI(umin)
      CASEI(imax)
      CASEI(umax)
      CASEI(iand)
      CASEI(ior)
      CASEI(ixor)
      CASEF(fadd)
      CASEF(fmul)
      CASEF(fmin)
      CASEF(fmax)
   default: UNREACHABLE("unknown reduction op");
#undef CASEI
#undef CASEF
   }
}

void
emit_uniform_subgroup(isel_context* ctx, nir_intrinsic_instr* instr, Temp src)
{
   Builder bld(ctx->program, ctx->block);
   Definition dst(get_ssa_temp(ctx, &instr->def));
   assert(dst.regClass().type() != RegType::vgpr);
   if (src.regClass().type() == RegType::vgpr)
      bld.pseudo(aco_opcode::p_as_uniform, dst, src);
   else
      bld.copy(dst, src);
}

void
emit_addition_uniform_reduce(isel_context* ctx, nir_op op, Definition dst, nir_src src, Temp count)
{
   Builder bld(ctx->program, ctx->block);
   Temp src_tmp = get_ssa_temp(ctx, src.ssa);

   if (op == nir_op_fadd) {
      src_tmp = as_vgpr(ctx, src_tmp);
      Temp tmp = dst.regClass() == s1 ? bld.tmp(RegClass::get(RegType::vgpr, src.ssa->bit_size / 8))
                                      : dst.getTemp();

      if (src.ssa->bit_size == 16) {
         count = bld.vop1(aco_opcode::v_cvt_f16_u16, bld.def(v2b), count);
         bld.vop2(aco_opcode::v_mul_f16, Definition(tmp), count, src_tmp);
      } else {
         assert(src.ssa->bit_size == 32);
         count = bld.vop1(aco_opcode::v_cvt_f32_u32, bld.def(v1), count);
         bld.vop2(aco_opcode::v_mul_f32, Definition(tmp), count, src_tmp);
      }

      if (tmp != dst.getTemp())
         bld.pseudo(aco_opcode::p_as_uniform, dst, tmp);

      return;
   }

   if (dst.regClass() == s1)
      src_tmp = bld.as_uniform(src_tmp);

   if (op == nir_op_ixor && count.type() == RegType::sgpr)
      count =
         bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), count, Operand::c32(1u));
   else if (op == nir_op_ixor)
      count = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand::c32(1u), count);

   assert(dst.getTemp().type() == count.type());

   if (nir_src_is_const(src)) {
      uint32_t imm = nir_src_as_uint(src);
      if (imm == 1 && dst.bytes() <= 2)
         bld.pseudo(aco_opcode::p_extract_vector, dst, count, Operand::zero());
      else if (imm == 1)
         bld.copy(dst, count);
      else if (imm == 0)
         bld.copy(dst, Operand::zero(dst.bytes()));
      else if (count.type() == RegType::vgpr)
         bld.v_mul_imm(dst, count, imm, true, true);
      else if (imm == 0xffffffff)
         bld.sop2(aco_opcode::s_sub_i32, dst, bld.def(s1, scc), Operand::zero(), count);
      else if (util_is_power_of_two_or_zero(imm))
         bld.sop2(aco_opcode::s_lshl_b32, dst, bld.def(s1, scc), count,
                  Operand::c32(ffs(imm) - 1u));
      else
         bld.sop2(aco_opcode::s_mul_i32, dst, src_tmp, count);
   } else if (dst.bytes() <= 2 && ctx->program->gfx_level >= GFX10) {
      bld.vop3(aco_opcode::v_mul_lo_u16_e64, dst, src_tmp, count);
   } else if (dst.bytes() <= 2 && ctx->program->gfx_level >= GFX8) {
      bld.vop2(aco_opcode::v_mul_lo_u16, dst, src_tmp, count);
   } else if (dst.getTemp().type() == RegType::vgpr) {
      bld.vop3(aco_opcode::v_mul_lo_u32, dst, src_tmp, count);
   } else {
      bld.sop2(aco_opcode::s_mul_i32, dst, src_tmp, count);
   }
}

bool
emit_uniform_reduce(isel_context* ctx, nir_intrinsic_instr* instr)
{
   nir_op op = (nir_op)nir_intrinsic_reduction_op(instr);
   if (op == nir_op_imul || op == nir_op_fmul)
      return false;

   if (op == nir_op_iadd || op == nir_op_ixor || op == nir_op_fadd) {
      Builder bld(ctx->program, ctx->block);
      Definition dst(get_ssa_temp(ctx, &instr->def));
      unsigned bit_size = instr->src[0].ssa->bit_size;
      if (bit_size > 32)
         return false;

      Temp thread_count =
         bld.sop1(Builder::s_bcnt1_i32, bld.def(s1), bld.def(s1, scc), Operand(exec, bld.lm));
      set_wqm(ctx);

      emit_addition_uniform_reduce(ctx, op, dst, instr->src[0], thread_count);
   } else {
      emit_uniform_subgroup(ctx, instr, get_ssa_temp(ctx, instr->src[0].ssa));
   }

   return true;
}

bool
emit_uniform_scan(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   Definition dst(get_ssa_temp(ctx, &instr->def));
   nir_op op = (nir_op)nir_intrinsic_reduction_op(instr);
   bool inc = instr->intrinsic == nir_intrinsic_inclusive_scan;

   if (op == nir_op_imul || op == nir_op_fmul)
      return false;

   if (op == nir_op_iadd || op == nir_op_ixor || op == nir_op_fadd) {
      if (instr->src[0].ssa->bit_size > 32)
         return false;

      Temp packed_tid;
      if (inc)
         packed_tid = emit_mbcnt(ctx, bld.tmp(v1), Operand(exec, bld.lm), Operand::c32(1u));
      else
         packed_tid = emit_mbcnt(ctx, bld.tmp(v1), Operand(exec, bld.lm));
      set_wqm(ctx);

      emit_addition_uniform_reduce(ctx, op, dst, instr->src[0], packed_tid);
      return true;
   }

   assert(op == nir_op_imin || op == nir_op_umin || op == nir_op_imax || op == nir_op_umax ||
          op == nir_op_iand || op == nir_op_ior || op == nir_op_fmin || op == nir_op_fmax);

   if (inc) {
      emit_uniform_subgroup(ctx, instr, get_ssa_temp(ctx, instr->src[0].ssa));
      return true;
   }

   /* Copy the source and write the reduction operation identity to the first lane. */
   Temp lane = bld.sop1(Builder::s_ff1_i32, bld.def(s1), Operand(exec, bld.lm));
   Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
   ReduceOp reduce_op = get_reduce_op(op, instr->src[0].ssa->bit_size);
   if (dst.bytes() == 8) {
      Temp lo = bld.tmp(v1), hi = bld.tmp(v1);
      bld.pseudo(aco_opcode::p_split_vector, Definition(lo), Definition(hi), src);
      uint32_t identity_lo = get_reduction_identity(reduce_op, 0);
      uint32_t identity_hi = get_reduction_identity(reduce_op, 1);

      lo =
         bld.writelane(bld.def(v1), bld.copy(bld.def(s1, m0), Operand::c32(identity_lo)), lane, lo);
      hi =
         bld.writelane(bld.def(v1), bld.copy(bld.def(s1, m0), Operand::c32(identity_hi)), lane, hi);
      bld.pseudo(aco_opcode::p_create_vector, dst, lo, hi);
   } else {
      uint32_t identity = get_reduction_identity(reduce_op, 0);
      bld.writelane(dst, bld.copy(bld.def(s1, m0), Operand::c32(identity)), lane,
                    as_vgpr(ctx, src));
   }

   set_wqm(ctx);
   return true;
}

Temp
emit_reduction_instr(isel_context* ctx, aco_opcode aco_op, ReduceOp op, unsigned cluster_size,
                     Definition dst, Temp src)
{
   assert(src.bytes() <= 8);
   assert(src.type() == RegType::vgpr);

   Builder bld(ctx->program, ctx->block);

   unsigned num_defs = 0;
   Definition defs[5];
   defs[num_defs++] = dst;
   defs[num_defs++] = bld.def(bld.lm); /* used internally to save/restore exec */

   /* scalar identity temporary */
   bool need_sitmp = (ctx->program->gfx_level <= GFX7 || ctx->program->gfx_level >= GFX10) &&
                     aco_op != aco_opcode::p_reduce;
   if (aco_op == aco_opcode::p_exclusive_scan) {
      need_sitmp |= (op == imin8 || op == imin16 || op == imin32 || op == imin64 || op == imax8 ||
                     op == imax16 || op == imax32 || op == imax64 || op == fmin16 || op == fmin32 ||
                     op == fmin64 || op == fmax16 || op == fmax32 || op == fmax64 || op == fmul16 ||
                     op == fmul64);
   }
   if (need_sitmp)
      defs[num_defs++] = bld.def(RegType::sgpr, dst.size());

   /* scc clobber */
   defs[num_defs++] = bld.def(s1, scc);

   /* vcc clobber */
   bool clobber_vcc = false;
   if ((op == iadd32 || op == imul64) && ctx->program->gfx_level < GFX9)
      clobber_vcc = true;
   if ((op == iadd8 || op == iadd16) && ctx->program->gfx_level < GFX8)
      clobber_vcc = true;
   if (op == iadd64 || op == umin64 || op == umax64 || op == imin64 || op == imax64)
      clobber_vcc = true;

   if (clobber_vcc)
      defs[num_defs++] = bld.def(bld.lm, vcc);

   Instruction* reduce = create_instruction(aco_op, Format::PSEUDO_REDUCTION, 3, num_defs);
   reduce->operands[0] = Operand(src);
   /* setup_reduce_temp will update these undef operands if needed */
   reduce->operands[1] = Operand(RegClass(RegType::vgpr, dst.size()).as_linear());
   reduce->operands[2] = Operand(v1.as_linear());
   std::copy(defs, defs + num_defs, reduce->definitions.begin());

   reduce->reduction().reduce_op = op;
   reduce->reduction().cluster_size = cluster_size;
   bld.insert(std::move(reduce));

   return dst.getTemp();
}

Temp
inclusive_scan_to_exclusive(isel_context* ctx, ReduceOp op, Definition dst, Temp src)
{
   Builder bld(ctx->program, ctx->block);

   Temp scan = emit_reduction_instr(ctx, aco_opcode::p_inclusive_scan, op, ctx->program->wave_size,
                                    bld.def(dst.regClass()), src);

   switch (op) {
   case iadd8:
   case iadd16:
   case iadd32: return bld.vsub32(dst, scan, src);
   case ixor64:
   case iadd64: {
      Temp src00 = bld.tmp(v1);
      Temp src01 = bld.tmp(v1);
      bld.pseudo(aco_opcode::p_split_vector, Definition(src00), Definition(src01), scan);
      Temp src10 = bld.tmp(v1);
      Temp src11 = bld.tmp(v1);
      bld.pseudo(aco_opcode::p_split_vector, Definition(src10), Definition(src11), src);

      Temp lower = bld.tmp(v1);
      Temp upper = bld.tmp(v1);
      if (op == iadd64) {
         Temp borrow = bld.vsub32(Definition(lower), src00, src10, true).def(1).getTemp();
         bld.vsub32(Definition(upper), src01, src11, false, borrow);
      } else {
         bld.vop2(aco_opcode::v_xor_b32, Definition(lower), src00, src10);
         bld.vop2(aco_opcode::v_xor_b32, Definition(upper), src01, src11);
      }
      return bld.pseudo(aco_opcode::p_create_vector, dst, lower, upper);
   }
   case ixor8:
   case ixor16:
   case ixor32: return bld.vop2(aco_opcode::v_xor_b32, dst, scan, src);
   default: UNREACHABLE("Unsupported op");
   }
}

bool
emit_rotate_by_constant(isel_context* ctx, Temp& dst, Temp src, unsigned cluster_size,
                        uint64_t delta)
{
   Builder bld(ctx->program, ctx->block);
   RegClass rc = src.regClass();
   dst = Temp(0, rc);
   delta %= cluster_size;

   if (delta == 0) {
      dst = bld.copy(bld.def(rc), src);
   } else if (delta * 2 == cluster_size && cluster_size <= 32) {
      dst = emit_masked_swizzle(ctx, bld, src, ds_pattern_bitmode(0x1f, 0, delta), true);
   } else if (cluster_size == 4) {
      unsigned res[4];
      for (unsigned i = 0; i < 4; i++)
         res[i] = (i + delta) & 0x3;
      uint32_t dpp_ctrl = dpp_quad_perm(res[0], res[1], res[2], res[3]);
      if (ctx->program->gfx_level >= GFX8)
         dst = bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(rc), src, dpp_ctrl);
      else
         dst = bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), src, (1 << 15) | dpp_ctrl);
   } else if (cluster_size == 8 && ctx->program->gfx_level >= GFX10) {
      uint32_t lane_sel = 0;
      for (unsigned i = 0; i < 8; i++)
         lane_sel |= ((i + delta) & 0x7) << (i * 3);
      dst = bld.vop1_dpp8(aco_opcode::v_mov_b32, bld.def(rc), src, lane_sel);
   } else if (cluster_size == 16 && ctx->program->gfx_level >= GFX8) {
      dst = bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(rc), src, dpp_row_rr(16 - delta));
   } else if (cluster_size <= 32 && ctx->program->gfx_level >= GFX8) {
      uint32_t ctrl = ds_pattern_rotate(delta, ~(cluster_size - 1) & 0x1f);
      dst = bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), src, ctrl);
   } else if (cluster_size == 64) {
      bool has_wf_dpp = ctx->program->gfx_level >= GFX8 && ctx->program->gfx_level < GFX10;
      if (delta == 32 && ctx->program->gfx_level >= GFX11) {
         dst = bld.vop1(aco_opcode::v_permlane64_b32, bld.def(rc), src);
      } else if (delta == 32 && can_use_shared_vgprs(ctx)) {
         enable_shared_vgprs(ctx);
         dst = bld.pseudo(aco_opcode::p_permlane64_shared_vgpr, bld.def(rc), src);
      } else if (delta == 1 && has_wf_dpp) {
         dst = bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(rc), src, dpp_wf_rl1);
      } else if (delta == 63 && has_wf_dpp) {
         dst = bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(rc), src, dpp_wf_rr1);
      }
   }

   return dst.id() != 0;
}

void
ds_ordered_count_offsets(isel_context* ctx, unsigned index_operand, unsigned wave_release,
                         unsigned wave_done, unsigned* offset0, unsigned* offset1)
{
   unsigned ordered_count_index = index_operand & 0x3f;
   unsigned count_dword = (index_operand >> 24) & 0xf;

   assert(ctx->options->gfx_level >= GFX10);
   assert(count_dword >= 1 && count_dword <= 4);

   *offset0 = ordered_count_index << 2;
   *offset1 = wave_release | (wave_done << 1) | ((count_dword - 1) << 6);

   if (ctx->options->gfx_level < GFX11)
      *offset1 |= 3 /* GS shader type */ << 2;
}

bool
get_replicated_constant(nir_def* def, unsigned stride, uint32_t* constant)
{
   nir_scalar comp = nir_scalar_resolved(def, 0);
   if (!nir_scalar_is_const(comp))
      return false;

   *constant = nir_scalar_as_uint(comp);

   for (unsigned i = stride; i < def->num_components; i += stride) {
      comp = nir_scalar_resolved(def, i);
      if (!nir_scalar_is_const(comp) || nir_scalar_as_uint(comp) != *constant)
         return false;
   }
   return true;
}

void
visit_cmat_muladd(isel_context* ctx, nir_intrinsic_instr* instr)
{
   aco_opcode opcode = aco_opcode::num_opcodes;

   bitarray8 neg_lo = nir_intrinsic_neg_lo_amd(instr);
   bitarray8 neg_hi = nir_intrinsic_neg_hi_amd(instr);

   enum glsl_base_type type_a = nir_intrinsic_src_base_type(instr);
   enum glsl_base_type type_b = nir_intrinsic_src_base_type2(instr);

   switch (type_a) {
   case GLSL_TYPE_FLOAT16:
      switch (instr->def.bit_size) {
      case 32: opcode = aco_opcode::v_wmma_f32_16x16x16_f16; break;
      case 16: opcode = aco_opcode::v_wmma_f16_16x16x16_f16; break;
      }
      break;
   case GLSL_TYPE_BFLOAT16:
      switch (instr->def.bit_size) {
      case 32: opcode = aco_opcode::v_wmma_f32_16x16x16_bf16; break;
      case 16: opcode = aco_opcode::v_wmma_bf16_16x16x16_bf16; break;
      }
      break;
   case GLSL_TYPE_UINT8:
   case GLSL_TYPE_INT8: {
      opcode = aco_opcode::v_wmma_i32_16x16x16_iu8;
      neg_lo[0] = type_a == GLSL_TYPE_INT8;
      neg_lo[1] = type_b == GLSL_TYPE_INT8;
      break;
   case GLSL_TYPE_FLOAT_E4M3FN:
      switch (type_b) {
      case GLSL_TYPE_FLOAT_E4M3FN: opcode = aco_opcode::v_wmma_f32_16x16x16_fp8_fp8; break;
      case GLSL_TYPE_FLOAT_E5M2: opcode = aco_opcode::v_wmma_f32_16x16x16_fp8_bf8; break;
      default: UNREACHABLE("invalid cmat_muladd_amd type");
      }
      break;
   case GLSL_TYPE_FLOAT_E5M2:
      switch (type_b) {
      case GLSL_TYPE_FLOAT_E4M3FN: opcode = aco_opcode::v_wmma_f32_16x16x16_bf8_fp8; break;
      case GLSL_TYPE_FLOAT_E5M2: opcode = aco_opcode::v_wmma_f32_16x16x16_bf8_bf8; break;
      default: UNREACHABLE("invalid cmat_muladd_amd type");
      }
      break;
   }
   default: UNREACHABLE("invalid cmat_muladd_amd type");
   }

   if (opcode == aco_opcode::num_opcodes)
      UNREACHABLE("visit_cmat_muladd: invalid bit size combination");

   Builder bld(ctx->program, ctx->block);

   Temp dst = get_ssa_temp(ctx, &instr->def);
   Operand A(as_vgpr(ctx, get_ssa_temp(ctx, instr->src[0].ssa)));
   Operand B(as_vgpr(ctx, get_ssa_temp(ctx, instr->src[1].ssa)));
   Operand C(as_vgpr(ctx, get_ssa_temp(ctx, instr->src[2].ssa)));

   uint32_t constant;
   uint32_t acc_stride = ctx->program->gfx_level < GFX12 && instr->def.bit_size == 16 ? 2 : 1;
   if (get_replicated_constant(instr->src[2].ssa, acc_stride, &constant)) {
      unsigned constant_size = instr->def.bit_size;
      if (opcode == aco_opcode::v_wmma_bf16_16x16x16_bf16) {
         /* Bfloat16 uses the high bits of 32bit inline constants. */
         constant <<= 16;
         constant_size = 32;
      }
      Operand constC = Operand::get_const(ctx->program->gfx_level, constant, constant_size / 8);
      if (!constC.isLiteral()) {
         C = constC;
      } else if (opcode != aco_opcode::v_wmma_i32_16x16x16_iu8) {
         constant ^= 1 << (constant_size - 1);
         constC = Operand::get_const(ctx->program->gfx_level, constant, constant_size / 8);
         if (!constC.isLiteral()) {
            C = constC;
            neg_lo[2] ^= !neg_hi[2];
         }
      }
   }

   VALU_instruction& vop3p = bld.vop3p(opcode, Definition(dst), A, B, C, 0, 0x7)->valu();
   vop3p.neg_lo = neg_lo;
   vop3p.neg_hi = neg_hi;
   vop3p.clamp = nir_intrinsic_saturate(instr);

   emit_split_vector(ctx, dst, instr->def.num_components);
}

void
pops_await_overlapped_waves(isel_context* ctx)
{
   ctx->program->has_pops_overlapped_waves_wait = true;

   Builder bld(ctx->program, ctx->block);

   if (ctx->program->gfx_level >= GFX11) {
      /* GFX11+ - waiting for the export from the overlapped waves.
       * Await the export_ready event (bit wait_event_imm_dont_wait_export_ready clear).
       */
      bld.sopp(aco_opcode::s_wait_event,
               ctx->program->gfx_level >= GFX12 ? wait_event_imm_wait_export_ready_gfx12 : 0);
      return;
   }

   /* Pre-GFX11 - sleep loop polling the exiting wave ID. */

   const Temp collision = get_arg(ctx, ctx->args->pops_collision_wave_id);

   /* Check if there's an overlap in the current wave - otherwise, the wait may result in a hang. */
   const Temp did_overlap =
      bld.sopc(aco_opcode::s_bitcmp1_b32, bld.def(s1, scc), collision, Operand::c32(31));
   if_context did_overlap_if_context;
   begin_uniform_if_then(ctx, &did_overlap_if_context, did_overlap);
   bld.reset(ctx->block);

   /* Set the packer register - after this, pops_exiting_wave_id can be polled. */
   if (ctx->program->gfx_level >= GFX10) {
      /* 2 packer ID bits on GFX10-10.3. */
      const Temp packer_id = bld.sop2(aco_opcode::s_bfe_u32, bld.def(s1), bld.def(s1, scc),
                                      collision, Operand::c32(0x2001c));
      /* POPS_PACKER register: bit 0 - POPS enabled for this wave, bits 2:1 - packer ID. */
      const Temp packer_id_hwreg_bits = bld.sop2(aco_opcode::s_lshl1_add_u32, bld.def(s1),
                                                 bld.def(s1, scc), packer_id, Operand::c32(1));
      bld.sopk(aco_opcode::s_setreg_b32, packer_id_hwreg_bits, ((3 - 1) << 11) | 25);
   } else {
      /* 1 packer ID bit on GFX9. */
      const Temp packer_id = bld.sop2(aco_opcode::s_bfe_u32, bld.def(s1), bld.def(s1, scc),
                                      collision, Operand::c32(0x1001c));
      /* MODE register: bit 24 - wave is associated with packer 0, bit 25 - with packer 1.
       * Packer index to packer bits: 0 to 0b01, 1 to 0b10.
       */
      const Temp packer_id_hwreg_bits =
         bld.sop2(aco_opcode::s_add_i32, bld.def(s1), bld.def(s1, scc), packer_id, Operand::c32(1));
      bld.sopk(aco_opcode::s_setreg_b32, packer_id_hwreg_bits, ((2 - 1) << 11) | (24 << 6) | 1);
   }

   Temp newest_overlapped_wave_id = bld.sop2(aco_opcode::s_bfe_u32, bld.def(s1), bld.def(s1, scc),
                                             collision, Operand::c32(0xa0010));
   if (ctx->program->gfx_level < GFX10) {
      /* On GFX9, the newest overlapped wave ID value passed to the shader is smaller than the
       * actual wave ID by 1 in case of wraparound.
       */
      const Temp current_wave_id = bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc),
                                            collision, Operand::c32(0x3ff));
      const Temp newest_overlapped_wave_id_wrapped = bld.sopc(
         aco_opcode::s_cmp_gt_u32, bld.def(s1, scc), newest_overlapped_wave_id, current_wave_id);
      newest_overlapped_wave_id =
         bld.sop2(aco_opcode::s_add_i32, bld.def(s1), bld.def(s1, scc), newest_overlapped_wave_id,
                  newest_overlapped_wave_id_wrapped);
   }

   /* The wave IDs are the low 10 bits of a monotonically increasing wave counter.
    * The overlapped and the exiting wave IDs can't be larger than the current wave ID, and they are
    * no more than 1023 values behind the current wave ID.
    * Remap the overlapped and the exiting wave IDs from wrapping to monotonic so an unsigned
    * comparison can be used: the wave `current - 1023` becomes 0, it's followed by a piece growing
    * away from 0, then a piece increasing until UINT32_MAX, and the current wave is UINT32_MAX.
    * To do that, subtract `current - 1023`, which with wrapping arithmetic is (current + 1), and
    * `a - (b + 1)` is `a + ~b`.
    * Note that if the 10-bit current wave ID is 1023 (thus 1024 will be subtracted), the wave
    * `current - 1023` will become `UINT32_MAX - 1023` rather than 0, but all the possible wave IDs
    * will still grow monotonically in the 32-bit value, and the unsigned comparison will behave as
    * expected.
    */
   const Temp wave_id_offset = bld.sop2(aco_opcode::s_nand_b32, bld.def(s1), bld.def(s1, scc),
                                        collision, Operand::c32(0x3ff));
   newest_overlapped_wave_id = bld.sop2(aco_opcode::s_add_i32, bld.def(s1), bld.def(s1, scc),
                                        newest_overlapped_wave_id, wave_id_offset);

   /* Await the overlapped waves. */

   loop_context wait_loop_context;
   begin_loop(ctx, &wait_loop_context);
   bld.reset(ctx->block);

   const Temp exiting_wave_id = bld.pseudo(aco_opcode::p_pops_gfx9_add_exiting_wave_id, bld.def(s1),
                                           bld.def(s1, scc), wave_id_offset);
   /* If the exiting (not exited) wave ID is larger than the newest overlapped wave ID (after
    * remapping both to monotonically increasing unsigned integers), the newest overlapped wave has
    * exited the ordered section.
    */
   const Temp newest_overlapped_wave_exited = bld.sopc(aco_opcode::s_cmp_lt_u32, bld.def(s1, scc),
                                                       newest_overlapped_wave_id, exiting_wave_id);
   if_context newest_overlapped_wave_exited_if_context;
   begin_uniform_if_then(ctx, &newest_overlapped_wave_exited_if_context,
                         newest_overlapped_wave_exited);
   emit_loop_break(ctx);
   begin_uniform_if_else(ctx, &newest_overlapped_wave_exited_if_context);
   end_uniform_if(ctx, &newest_overlapped_wave_exited_if_context);
   bld.reset(ctx->block);

   /* Sleep before rechecking to let overlapped waves run for some time. */
   bld.sopp(aco_opcode::s_sleep, ctx->program->gfx_level >= GFX10 ? UINT16_MAX : 3);

   end_loop(ctx, &wait_loop_context);
   bld.reset(ctx->block);

   /* Indicate the wait has been done to subsequent compilation stages. */
   bld.pseudo(aco_opcode::p_pops_gfx9_overlapped_wave_wait_done);

   begin_uniform_if_else(ctx, &did_overlap_if_context);
   end_uniform_if(ctx, &did_overlap_if_context);
   bld.reset(ctx->block);
}

uint16_t
ds_bvh_stack_offset1_gfx11(unsigned stack_size)
{
   switch (stack_size) {
   case 8: return 0x00;
   case 16: return 0x10;
   case 32: return 0x20;
   case 64: return 0x30;
   default: UNREACHABLE("invalid stack size");
   }
}

void
emit_ds_bvh_stack_push4_pop1_rtn(isel_context* ctx, nir_intrinsic_instr* instr, Builder& bld)
{
   Temp dst = get_ssa_temp(ctx, &instr->def);
   Temp stack_addr = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[0].ssa));
   Temp last_node = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[1].ssa));
   Temp intersection_result = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[2].ssa));

   Temp dst_stack_addr = bld.tmp(v1);
   Temp dst_node_pointer = bld.tmp(v1);
   uint32_t offset0 = 0, offset1 = 0;
   if (ctx->program->gfx_level >= GFX12)
      offset0 = nir_intrinsic_stack_size(instr);
   else
      offset1 = ds_bvh_stack_offset1_gfx11(nir_intrinsic_stack_size(instr));
   bld.ds(aco_opcode::ds_bvh_stack_push4_pop1_rtn_b32, Definition(dst_stack_addr),
          Definition(dst_node_pointer), Operand(stack_addr), Operand(last_node),
          Operand(intersection_result), offset0, offset1);
   bld.pseudo(aco_opcode::p_create_vector, Definition(dst), Operand(dst_stack_addr),
              Operand(dst_node_pointer));
}

void
emit_ds_bvh_stack_push8_pop1_rtn(isel_context* ctx, nir_intrinsic_instr* instr, Builder& bld)
{
   Temp dst = get_ssa_temp(ctx, &instr->def);
   Temp stack_addr = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[0].ssa));
   Temp last_node = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[1].ssa));
   Temp intersection_result = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[2].ssa));

   Temp dst_stack_addr = bld.tmp(v1);
   Temp dst_node_pointer = bld.tmp(v1);
   Instruction* ds_instr =
      create_instruction(aco_opcode::ds_bvh_stack_push8_pop1_rtn_b32, Format::DS, 2 + 8, 2);
   ds_instr->definitions[0] = Definition(dst_stack_addr);
   ds_instr->definitions[1] = Definition(dst_node_pointer);
   ds_instr->operands[0] = Operand(stack_addr);
   ds_instr->operands[1] = Operand(last_node);
   for (unsigned i = 0; i < 8; ++i) {
      ds_instr->operands[2 + i] = Operand(emit_extract_vector(ctx, intersection_result, i, v1));
      if (i < 7)
         ds_instr->operands[2 + i].setVectorAligned(true);
   }
   ds_instr->ds().offset0 = nir_intrinsic_stack_size(instr);
   bld.insert(aco_ptr<Instruction>(ds_instr));
   bld.pseudo(aco_opcode::p_create_vector, Definition(dst), Operand(dst_stack_addr),
              Operand(dst_node_pointer));
}

} // namespace

void
visit_intrinsic(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   switch (instr->intrinsic) {
   case nir_intrinsic_load_interpolated_input: visit_load_interpolated_input(ctx, instr); break;
   case nir_intrinsic_store_output: visit_store_output(ctx, instr); break;
   case nir_intrinsic_load_input:
   case nir_intrinsic_load_per_primitive_input:
   case nir_intrinsic_load_input_vertex:
      if (ctx->program->stage == fragment_fs)
         visit_load_fs_input(ctx, instr);
      else
         isel_err(&instr->instr, "Shader inputs should have been lowered in NIR.");
      break;
   case nir_intrinsic_load_per_vertex_input: visit_load_per_vertex_input(ctx, instr); break;
   case nir_intrinsic_load_ubo: visit_load_ubo(ctx, instr); break;
   case nir_intrinsic_load_constant: visit_load_constant(ctx, instr); break;
   case nir_intrinsic_load_shared: visit_load_shared(ctx, instr); break;
   case nir_intrinsic_store_shared: visit_store_shared(ctx, instr); break;
   case nir_intrinsic_shared_atomic:
   case nir_intrinsic_shared_atomic_swap: visit_shared_atomic(ctx, instr); break;
   case nir_intrinsic_shared_append_amd:
   case nir_intrinsic_shared_consume_amd: visit_shared_append(ctx, instr); break;
   case nir_intrinsic_load_shared2_amd:
   case nir_intrinsic_store_shared2_amd: visit_access_shared2_amd(ctx, instr); break;
   case nir_intrinsic_bindless_image_load:
   case nir_intrinsic_bindless_image_fragment_mask_load_amd:
   case nir_intrinsic_bindless_image_sparse_load: visit_image_load(ctx, instr); break;
   case nir_intrinsic_bindless_image_store: visit_image_store(ctx, instr); break;
   case nir_intrinsic_bindless_image_atomic:
   case nir_intrinsic_bindless_image_atomic_swap: visit_image_atomic(ctx, instr); break;
   case nir_intrinsic_load_ssbo: visit_load_ssbo(ctx, instr); break;
   case nir_intrinsic_store_ssbo: visit_store_ssbo(ctx, instr); break;
   case nir_intrinsic_load_typed_buffer_amd:
   case nir_intrinsic_load_buffer_amd: visit_load_buffer(ctx, instr); break;
   case nir_intrinsic_store_buffer_amd: visit_store_buffer(ctx, instr); break;
   case nir_intrinsic_load_global_amd: visit_load_global(ctx, instr); break;
   case nir_intrinsic_store_global_amd: visit_store_global(ctx, instr); break;
   case nir_intrinsic_global_atomic_amd:
   case nir_intrinsic_global_atomic_swap_amd: visit_global_atomic(ctx, instr); break;
   case nir_intrinsic_ssbo_atomic:
   case nir_intrinsic_ssbo_atomic_swap: visit_atomic_ssbo(ctx, instr); break;
   case nir_intrinsic_load_scratch: visit_load_scratch(ctx, instr); break;
   case nir_intrinsic_store_scratch: visit_store_scratch(ctx, instr); break;
   case nir_intrinsic_barrier: emit_barrier(ctx, instr); break;
   case nir_intrinsic_load_num_workgroups: {
      Temp dst = get_ssa_temp(ctx, &instr->def);
      if (ctx->options->load_grid_size_from_user_sgpr) {
         bld.copy(Definition(dst), get_arg(ctx, ctx->args->num_work_groups));
      } else {
         Temp addr = get_arg(ctx, ctx->args->num_work_groups);
         assert(addr.regClass() == s2);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst),
                    bld.smem(aco_opcode::s_load_dwordx2, bld.def(s2), addr, Operand::zero()),
                    bld.smem(aco_opcode::s_load_dword, bld.def(s1), addr, Operand::c32(8)));
      }
      emit_split_vector(ctx, dst, 3);
      break;
   }
   case nir_intrinsic_load_workgroup_id: {
      Temp dst = get_ssa_temp(ctx, &instr->def);
      if (ctx->stage.hw == AC_HW_COMPUTE_SHADER) {
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), ctx->workgroup_id[0],
                    ctx->workgroup_id[1], ctx->workgroup_id[2]);
         emit_split_vector(ctx, dst, 3);
      } else {
         isel_err(&instr->instr, "Unsupported stage for load_workgroup_id");
      }
      break;
   }
   case nir_intrinsic_load_subgroup_id: {
      assert(ctx->options->gfx_level >= GFX12 && ctx->stage.hw == AC_HW_COMPUTE_SHADER);
      bld.sop2(aco_opcode::s_bfe_u32, Definition(get_ssa_temp(ctx, &instr->def)), bld.def(s1, scc),
               ctx->ttmp8, Operand::c32(25 | (5 << 16)));
      break;
   }
   case nir_intrinsic_ddx:
   case nir_intrinsic_ddy:
   case nir_intrinsic_ddx_fine:
   case nir_intrinsic_ddy_fine:
   case nir_intrinsic_ddx_coarse:
   case nir_intrinsic_ddy_coarse: {
      Temp src = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[0].ssa));
      Temp dst = get_ssa_temp(ctx, &instr->def);

      uint16_t dpp_ctrl1, dpp_ctrl2;
      if (instr->intrinsic == nir_intrinsic_ddx_fine) {
         if (nir_def_all_uses_ignore_sign_bit(&instr->def)) {
            dpp_ctrl1 = dpp_quad_perm(1, 0, 3, 2);
            dpp_ctrl2 = dpp_quad_perm(0, 1, 2, 3);
         } else {
            dpp_ctrl1 = dpp_quad_perm(0, 0, 2, 2);
            dpp_ctrl2 = dpp_quad_perm(1, 1, 3, 3);
         }
      } else if (instr->intrinsic == nir_intrinsic_ddy_fine) {
         if (nir_def_all_uses_ignore_sign_bit(&instr->def)) {
            dpp_ctrl1 = dpp_quad_perm(2, 3, 0, 1);
            dpp_ctrl2 = dpp_quad_perm(0, 1, 2, 3);
         } else {
            dpp_ctrl1 = dpp_quad_perm(0, 1, 0, 1);
            dpp_ctrl2 = dpp_quad_perm(2, 3, 2, 3);
         }
      } else {
         dpp_ctrl1 = dpp_quad_perm(0, 0, 0, 0);
         if (instr->intrinsic == nir_intrinsic_ddx || instr->intrinsic == nir_intrinsic_ddx_coarse)
            dpp_ctrl2 = dpp_quad_perm(1, 1, 1, 1);
         else
            dpp_ctrl2 = dpp_quad_perm(2, 2, 2, 2);
      }

      if (dst.regClass() == v1 && instr->def.bit_size == 16) {
         assert(instr->def.num_components == 2);

         /* identify swizzle to opsel */
         unsigned opsel_lo = 0b00;
         unsigned opsel_hi = 0b11;

         Temp tl = src;
         if (nir_src_is_divergent(&instr->src[0]))
            tl = bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(v1), src, dpp_ctrl1);

         Builder::Result sub =
            bld.vop3p(aco_opcode::v_pk_add_f16, bld.def(v1), src, tl, opsel_lo, opsel_hi);
         sub->valu().neg_lo[1] = true;
         sub->valu().neg_hi[1] = true;

         if (nir_src_is_divergent(&instr->src[0]) && dpp_ctrl2 != dpp_quad_perm(0, 1, 2, 3))
            bld.vop1_dpp(aco_opcode::v_mov_b32, Definition(dst), sub, dpp_ctrl2);
         else
            bld.copy(Definition(dst), sub);
         emit_split_vector(ctx, dst, 2);
      } else {
         aco_opcode subrev =
            instr->def.bit_size == 16 ? aco_opcode::v_subrev_f16 : aco_opcode::v_subrev_f32;

         if (!nir_src_is_divergent(&instr->src[0])) {
            bld.vop2(subrev, Definition(dst), src, src);
         } else if (ctx->program->gfx_level >= GFX8 && dpp_ctrl2 == dpp_quad_perm(0, 1, 2, 3)) {
            bld.vop2_dpp(subrev, Definition(dst), src, src, dpp_ctrl1);
         } else if (ctx->program->gfx_level >= GFX8) {
            Temp tmp = bld.vop2_dpp(subrev, bld.def(v1), src, src, dpp_ctrl1);
            bld.vop1_dpp(aco_opcode::v_mov_b32, Definition(dst), tmp, dpp_ctrl2);
         } else {
            Temp tl = bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), src, (1 << 15) | dpp_ctrl1);
            Temp tr = src;
            if (dpp_ctrl2 != dpp_quad_perm(0, 1, 2, 3))
               tr = bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), src, (1 << 15) | dpp_ctrl2);
            bld.vop2(subrev, Definition(dst), tl, tr);
         }
      }
      set_wqm(ctx, true);
      break;
   }

   case nir_intrinsic_ballot_relaxed:
   case nir_intrinsic_ballot: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      Temp dst = get_ssa_temp(ctx, &instr->def);

      if (instr->src[0].ssa->bit_size == 1) {
         assert(src.regClass() == bld.lm);
      } else if (instr->src[0].ssa->bit_size == 32 && src.regClass() == v1) {
         src = bld.vopc(aco_opcode::v_cmp_lg_u32, bld.def(bld.lm), Operand::zero(), src);
      } else if (instr->src[0].ssa->bit_size == 64 && src.regClass() == v2) {
         src = bld.vopc(aco_opcode::v_cmp_lg_u64, bld.def(bld.lm), Operand::zero(), src);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }

      /* Make sure that all inactive lanes return zero.
       * Value-numbering might remove the comparison above */
      Definition def = dst.size() == bld.lm.size() ? Definition(dst) : bld.def(bld.lm);
      if (instr->intrinsic == nir_intrinsic_ballot_relaxed)
         src = bld.copy(def, src);
      else
         src = bld.sop2(Builder::s_and, def, bld.def(s1, scc), src, Operand(exec, bld.lm));
      if (dst.size() != bld.lm.size()) {
         /* Wave32 with ballot size set to 64 */
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), src, Operand::zero());
      }

      set_wqm(ctx);
      break;
   }
   case nir_intrinsic_inverse_ballot: {
      Temp src = bld.as_uniform(get_ssa_temp(ctx, instr->src[0].ssa));
      Temp dst = get_ssa_temp(ctx, &instr->def);

      assert(dst.size() == bld.lm.size());
      if (src.size() > dst.size()) {
         emit_extract_vector(ctx, src, 0, dst);
      } else if (src.size() < dst.size()) {
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), src, Operand::zero());
      } else {
         bld.copy(Definition(dst), src);
      }
      break;
   }
   case nir_intrinsic_shuffle:
   case nir_intrinsic_read_invocation: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      assert(instr->def.bit_size != 1);
      if (!nir_src_is_divergent(&instr->src[0])) {
         emit_uniform_subgroup(ctx, instr, src);
      } else {
         Temp tid = get_ssa_temp(ctx, instr->src[1].ssa);
         if (instr->intrinsic == nir_intrinsic_read_invocation ||
             !nir_src_is_divergent(&instr->src[1]))
            tid = bld.as_uniform(tid);
         Temp dst = get_ssa_temp(ctx, &instr->def);

         src = as_vgpr(ctx, src);

         if (src.regClass() == v1b || src.regClass() == v2b) {
            Temp tmp = bld.tmp(v1);
            tmp = emit_bpermute(ctx, bld, tid, src);
            if (dst.type() == RegType::vgpr)
               bld.pseudo(aco_opcode::p_split_vector, Definition(dst),
                          bld.def(src.regClass() == v1b ? v3b : v2b), tmp);
            else
               bld.pseudo(aco_opcode::p_as_uniform, Definition(dst), tmp);
         } else if (src.regClass() == v1) {
            Temp tmp = emit_bpermute(ctx, bld, tid, src);
            bld.copy(Definition(dst), tmp);
         } else if (src.regClass() == v2) {
            Temp lo = bld.tmp(v1), hi = bld.tmp(v1);
            bld.pseudo(aco_opcode::p_split_vector, Definition(lo), Definition(hi), src);
            lo = emit_bpermute(ctx, bld, tid, lo);
            hi = emit_bpermute(ctx, bld, tid, hi);
            bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lo, hi);
            emit_split_vector(ctx, dst, 2);
         } else {
            isel_err(&instr->instr, "Unimplemented NIR instr bit size");
         }
         set_wqm(ctx);
      }
      break;
   }
   case nir_intrinsic_rotate: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      Temp delta = get_ssa_temp(ctx, instr->src[1].ssa);
      Temp dst = get_ssa_temp(ctx, &instr->def);
      assert(instr->def.bit_size > 1 && instr->def.bit_size <= 32);

      if (!nir_src_is_divergent(&instr->src[0])) {
         emit_uniform_subgroup(ctx, instr, src);
         break;
      }

      unsigned cluster_size = nir_intrinsic_cluster_size(instr);
      cluster_size = util_next_power_of_two(
         MIN2(cluster_size ? cluster_size : ctx->program->wave_size, ctx->program->wave_size));

      if (cluster_size == 1) {
         bld.copy(Definition(dst), src);
         break;
      }

      delta = bld.as_uniform(delta);
      src = as_vgpr(ctx, src);

      Temp tmp;
      if (nir_src_is_const(instr->src[1]) &&
          emit_rotate_by_constant(ctx, tmp, src, cluster_size, nir_src_as_uint(instr->src[1]))) {
      } else if (cluster_size == 2) {
         Temp noswap =
            bld.sopc(aco_opcode::s_bitcmp0_b32, bld.def(s1, scc), delta, Operand::c32(0));
         noswap = bool_to_vector_condition(ctx, noswap);
         Temp swapped = emit_masked_swizzle(ctx, bld, src, ds_pattern_bitmode(0x1f, 0, 0x1), true);
         tmp = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(src.regClass()), swapped, src, noswap);
      } else if (ctx->program->gfx_level >= GFX10 && cluster_size <= 16) {
         if (cluster_size == 4) /* shift mask already does this for 8/16. */
            delta = bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), delta,
                             Operand::c32(0x3));
         delta =
            bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1), bld.def(s1, scc), delta, Operand::c32(2));

         Temp lo = bld.copy(bld.def(s1), Operand::c32(cluster_size == 4 ? 0x32103210 : 0x76543210));
         Temp hi;

         if (cluster_size <= 8) {
            Temp shr = bld.sop2(aco_opcode::s_lshr_b32, bld.def(s1), bld.def(s1, scc), lo, delta);
            if (cluster_size == 4) {
               Temp lotolohi = bld.copy(bld.def(s1), Operand::c32(0x4444));
               Temp lohi =
                  bld.sop2(aco_opcode::s_or_b32, bld.def(s1), bld.def(s1, scc), shr, lotolohi);
               lo = bld.sop2(aco_opcode::s_pack_ll_b32_b16, bld.def(s1), shr, lohi);
            } else {
               delta = bld.sop2(aco_opcode::s_sub_u32, bld.def(s1), bld.def(s1, scc),
                                Operand::c32(32), delta);
               Temp shl =
                  bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1), bld.def(s1, scc), lo, delta);
               lo = bld.sop2(aco_opcode::s_or_b32, bld.def(s1), bld.def(s1, scc), shr, shl);
            }
            Temp lotohi = bld.copy(bld.def(s1), Operand::c32(0x88888888));
            hi = bld.sop2(aco_opcode::s_or_b32, bld.def(s1), bld.def(s1, scc), lo, lotohi);
         } else {
            hi = bld.copy(bld.def(s1), Operand::c32(0xfedcba98));

            Temp lohi = bld.pseudo(aco_opcode::p_create_vector, bld.def(s2), lo, hi);

            Temp shr = bld.sop2(aco_opcode::s_lshr_b64, bld.def(s2), bld.def(s1, scc), lohi, delta);
            delta = bld.sop2(aco_opcode::s_sub_u32, bld.def(s1), bld.def(s1, scc), Operand::c32(64),
                             delta);
            Temp shl = bld.sop2(aco_opcode::s_lshl_b64, bld.def(s2), bld.def(s1, scc), lohi, delta);

            lohi = bld.sop2(aco_opcode::s_or_b64, bld.def(s2), bld.def(s1, scc), shr, shl);
            lo = bld.tmp(s1);
            hi = bld.tmp(s1);
            bld.pseudo(aco_opcode::p_split_vector, Definition(lo), Definition(hi), lohi);
         }

         Builder::Result ret =
            bld.vop3(aco_opcode::v_permlane16_b32, bld.def(src.regClass()), src, lo, hi);
         ret->valu().opsel[0] = true; /* set FETCH_INACTIVE */
         ret->valu().opsel[1] = true; /* set BOUND_CTRL */
         tmp = ret;
      } else {
         /* Fallback to ds_bpermute if we can't find a special instruction. */
         Temp tid = emit_mbcnt(ctx, bld.tmp(v1));
         Temp src_lane = bld.vadd32(bld.def(v1), tid, delta);

         if (ctx->program->gfx_level >= GFX10 && ctx->program->gfx_level <= GFX11_5 &&
             cluster_size == 32) {
            /* ds_bpermute is restricted to 32 lanes on GFX10-GFX11.5. */
            Temp index_x4 =
               bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand::c32(2u), src_lane);
            tmp = bld.ds(aco_opcode::ds_bpermute_b32, bld.def(v1), index_x4, src);
         } else {
            /* Technically, full wave rotate doesn't need this, but it breaks the pseudo ops. */
            src_lane = bld.vop3(aco_opcode::v_bfi_b32, bld.def(v1), Operand::c32(cluster_size - 1),
                                src_lane, tid);
            tmp = emit_bpermute(ctx, bld, src_lane, src);
         }
      }

      tmp = emit_extract_vector(ctx, tmp, 0, dst.regClass());
      bld.copy(Definition(dst), tmp);
      set_wqm(ctx);
      break;
   }
   case nir_intrinsic_read_first_invocation: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      Temp dst = get_ssa_temp(ctx, &instr->def);
      if (instr->def.bit_size == 1) {
         assert(src.regClass() == bld.lm);
         Temp tmp = bld.sopc(Builder::s_bitcmp1, bld.def(s1, scc), src,
                             bld.sop1(Builder::s_ff1_i32, bld.def(s1), Operand(exec, bld.lm)));
         bool_to_vector_condition(ctx, tmp, dst);
      } else {
         emit_vector_as_uniform(ctx, src, dst, true);
      }
      set_wqm(ctx);
      break;
   }
   case nir_intrinsic_as_uniform: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      Temp dst = get_ssa_temp(ctx, &instr->def);
      if (src.type() == RegType::vgpr)
         bld.pseudo(aco_opcode::p_as_uniform, Definition(dst), src);
      else
         bld.copy(Definition(dst), src);
      break;
   }
   case nir_intrinsic_vote_all: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      Temp dst = get_ssa_temp(ctx, &instr->def);
      assert(src.regClass() == bld.lm);
      assert(dst.regClass() == bld.lm);

      if (!nir_src_is_divergent(&instr->src[0])) {
         bld.copy(Definition(dst), src);
         break;
      }

      Temp tmp = bld.sop1(Builder::s_not, bld.def(bld.lm), bld.def(s1, scc), src);
      tmp = bld.sop2(Builder::s_and, bld.def(bld.lm), bld.def(s1, scc), tmp, Operand(exec, bld.lm))
               .def(1)
               .getTemp();
      Temp cond = bool_to_vector_condition(ctx, tmp);
      bld.sop1(Builder::s_not, Definition(dst), bld.def(s1, scc), cond);
      set_wqm(ctx);
      break;
   }
   case nir_intrinsic_vote_any: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      Temp dst = get_ssa_temp(ctx, &instr->def);
      assert(src.regClass() == bld.lm);
      assert(dst.regClass() == bld.lm);

      if (!nir_src_is_divergent(&instr->src[0])) {
         bld.copy(Definition(dst), src);
         break;
      }

      Temp tmp = bool_to_scalar_condition(ctx, src);
      bool_to_vector_condition(ctx, tmp, dst);
      set_wqm(ctx);
      break;
   }
   case nir_intrinsic_quad_vote_any: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      if (!nir_src_is_divergent(&instr->src[0])) {
         bld.copy(Definition(get_ssa_temp(ctx, &instr->def)), src);
         break;
      }

      src = bld.sop2(Builder::s_and, bld.def(bld.lm), bld.def(s1, scc), src, Operand(exec, bld.lm));
      bld.sop1(Builder::s_wqm, Definition(get_ssa_temp(ctx, &instr->def)), bld.def(s1, scc), src);
      set_wqm(ctx);
      break;
   }
   case nir_intrinsic_quad_vote_all: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      if (!nir_src_is_divergent(&instr->src[0])) {
         bld.copy(Definition(get_ssa_temp(ctx, &instr->def)), src);
         break;
      }

      src = bld.sop1(Builder::s_not, bld.def(bld.lm), bld.def(s1, scc), src);
      src = bld.sop2(Builder::s_and, bld.def(bld.lm), bld.def(s1, scc), src, Operand(exec, bld.lm));
      src = bld.sop1(Builder::s_wqm, bld.def(bld.lm), bld.def(s1, scc), src);
      bld.sop1(Builder::s_not, Definition(get_ssa_temp(ctx, &instr->def)), bld.def(s1, scc), src);
      set_wqm(ctx);
      break;
   }
   case nir_intrinsic_reduce:
   case nir_intrinsic_inclusive_scan:
   case nir_intrinsic_exclusive_scan: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      Temp dst = get_ssa_temp(ctx, &instr->def);
      nir_op op = (nir_op)nir_intrinsic_reduction_op(instr);
      unsigned cluster_size =
         instr->intrinsic == nir_intrinsic_reduce ? nir_intrinsic_cluster_size(instr) : 0;
      cluster_size = util_next_power_of_two(
         MIN2(cluster_size ? cluster_size : ctx->program->wave_size, ctx->program->wave_size));
      const unsigned bit_size = instr->src[0].ssa->bit_size;
      assert(bit_size != 1);

      if (!nir_src_is_divergent(&instr->src[0])) {
         /* We use divergence analysis to assign the regclass, so check if it's
          * working as expected */
         ASSERTED bool expected_divergent = instr->intrinsic == nir_intrinsic_exclusive_scan;
         if (instr->intrinsic == nir_intrinsic_inclusive_scan ||
             cluster_size != ctx->program->wave_size)
            expected_divergent = op == nir_op_iadd || op == nir_op_fadd || op == nir_op_ixor ||
                                 op == nir_op_imul || op == nir_op_fmul;
         assert(instr->def.divergent == expected_divergent);

         if (instr->intrinsic == nir_intrinsic_reduce) {
            if (!instr->def.divergent && emit_uniform_reduce(ctx, instr))
               break;
         } else if (emit_uniform_scan(ctx, instr)) {
            break;
         }
      }

      src = emit_extract_vector(ctx, src, 0, RegClass::get(RegType::vgpr, bit_size / 8));
      ReduceOp reduce_op = get_reduce_op(op, bit_size);

      aco_opcode aco_op;
      switch (instr->intrinsic) {
      case nir_intrinsic_reduce: aco_op = aco_opcode::p_reduce; break;
      case nir_intrinsic_inclusive_scan: aco_op = aco_opcode::p_inclusive_scan; break;
      case nir_intrinsic_exclusive_scan: aco_op = aco_opcode::p_exclusive_scan; break;
      default: UNREACHABLE("unknown reduce intrinsic");
      }

      /* Avoid whole wave shift. */
      const bool use_inclusive_for_exclusive = aco_op == aco_opcode::p_exclusive_scan &&
                                               (op == nir_op_iadd || op == nir_op_ixor) &&
                                               dst.type() == RegType::vgpr;
      if (use_inclusive_for_exclusive)
         inclusive_scan_to_exclusive(ctx, reduce_op, Definition(dst), src);
      else
         emit_reduction_instr(ctx, aco_op, reduce_op, cluster_size, Definition(dst), src);

      set_wqm(ctx);
      break;
   }
   case nir_intrinsic_dpp16_shift_amd: {
      Temp src = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[0].ssa));
      Temp dst = get_ssa_temp(ctx, &instr->def);
      int delta = nir_intrinsic_base(instr);
      assert(delta >= -15 && delta <= 15 && delta != 0);
      assert(instr->def.bit_size != 1 && instr->def.bit_size < 64);
      assert(ctx->options->gfx_level >= GFX8);

      uint16_t dpp_ctrl = delta < 0 ? dpp_row_sr(-delta) : dpp_row_sl(delta);
      bld.vop1_dpp(aco_opcode::v_mov_b32, Definition(dst), src, dpp_ctrl);

      set_wqm(ctx);
      break;
   }
   case nir_intrinsic_quad_broadcast:
   case nir_intrinsic_quad_swap_horizontal:
   case nir_intrinsic_quad_swap_vertical:
   case nir_intrinsic_quad_swap_diagonal:
   case nir_intrinsic_quad_swizzle_amd: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);

      if (!instr->def.divergent) {
         emit_uniform_subgroup(ctx, instr, src);
         break;
      }

      /* Quad broadcast lane. */
      unsigned lane = 0;
      /* Use VALU for the bool instructions that don't have a SALU-only special case. */
      bool bool_use_valu = instr->def.bit_size == 1;

      uint16_t dpp_ctrl = 0;

      bool allow_fi = true;
      switch (instr->intrinsic) {
      case nir_intrinsic_quad_swap_horizontal: dpp_ctrl = dpp_quad_perm(1, 0, 3, 2); break;
      case nir_intrinsic_quad_swap_vertical: dpp_ctrl = dpp_quad_perm(2, 3, 0, 1); break;
      case nir_intrinsic_quad_swap_diagonal: dpp_ctrl = dpp_quad_perm(3, 2, 1, 0); break;
      case nir_intrinsic_quad_swizzle_amd:
         dpp_ctrl = nir_intrinsic_swizzle_mask(instr);
         allow_fi &= nir_intrinsic_fetch_inactive(instr);
         break;
      case nir_intrinsic_quad_broadcast:
         lane = nir_src_as_const_value(instr->src[1])->u32;
         dpp_ctrl = dpp_quad_perm(lane, lane, lane, lane);
         bool_use_valu = false;
         break;
      default: break;
      }

      Temp dst = get_ssa_temp(ctx, &instr->def);

      /* Setup source. */
      if (bool_use_valu)
         src = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), Operand::zero(),
                            Operand::c32(-1), src);
      else if (instr->def.bit_size != 1)
         src = as_vgpr(ctx, src);

      if (instr->def.bit_size == 1 && instr->intrinsic == nir_intrinsic_quad_broadcast) {
         /* Special case for quad broadcast using SALU only. */
         assert(src.regClass() == bld.lm && dst.regClass() == bld.lm);

         uint32_t half_mask = 0x11111111u << lane;
         Operand mask_tmp = bld.lm.bytes() == 4
                               ? Operand::c32(half_mask)
                               : bld.pseudo(aco_opcode::p_create_vector, bld.def(bld.lm),
                                            Operand::c32(half_mask), Operand::c32(half_mask));

         src =
            bld.sop2(Builder::s_and, bld.def(bld.lm), bld.def(s1, scc), src, Operand(exec, bld.lm));
         src = bld.sop2(Builder::s_and, bld.def(bld.lm), bld.def(s1, scc), mask_tmp, src);
         bld.sop1(Builder::s_wqm, Definition(dst), bld.def(s1, scc), src);
      } else if (instr->def.bit_size <= 32 || bool_use_valu) {
         unsigned excess_bytes = bool_use_valu ? 0 : 4 - instr->def.bit_size / 8;
         Definition def = (excess_bytes || bool_use_valu) ? bld.def(v1) : Definition(dst);

         if (ctx->program->gfx_level >= GFX8)
            bld.vop1_dpp(aco_opcode::v_mov_b32, def, src, dpp_ctrl, 0xf, 0xf, true, allow_fi);
         else
            bld.ds(aco_opcode::ds_swizzle_b32, def, src, (1 << 15) | dpp_ctrl);

         if (excess_bytes)
            bld.pseudo(aco_opcode::p_split_vector, Definition(dst),
                       bld.def(RegClass::get(dst.type(), excess_bytes)), def.getTemp());
         if (bool_use_valu)
            bld.vopc(aco_opcode::v_cmp_lg_u32, Definition(dst), Operand::zero(), def.getTemp());
      } else if (instr->def.bit_size == 64) {
         Temp lo = bld.tmp(v1), hi = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lo), Definition(hi), src);

         if (ctx->program->gfx_level >= GFX8) {
            lo = bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(v1), lo, dpp_ctrl, 0xf, 0xf, true,
                              allow_fi);
            hi = bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(v1), hi, dpp_ctrl, 0xf, 0xf, true,
                              allow_fi);
         } else {
            lo = bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), lo, (1 << 15) | dpp_ctrl);
            hi = bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), hi, (1 << 15) | dpp_ctrl);
         }

         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lo, hi);
         emit_split_vector(ctx, dst, 2);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR quad group instruction bit size.");
      }

      set_wqm(ctx);
      break;
   }
   case nir_intrinsic_masked_swizzle_amd: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      if (!instr->def.divergent) {
         emit_uniform_subgroup(ctx, instr, src);
         break;
      }
      Temp dst = get_ssa_temp(ctx, &instr->def);
      uint32_t mask = nir_intrinsic_swizzle_mask(instr);
      bool allow_fi = nir_intrinsic_fetch_inactive(instr);

      if (instr->def.bit_size != 1)
         src = as_vgpr(ctx, src);

      if (instr->def.bit_size == 1) {
         assert(src.regClass() == bld.lm);
         src = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), Operand::zero(),
                            Operand::c32(-1), src);
         src = emit_masked_swizzle(ctx, bld, src, mask, allow_fi);
         bld.vopc(aco_opcode::v_cmp_lg_u32, Definition(dst), Operand::zero(), src);
      } else if (dst.regClass() == v1b) {
         Temp tmp = emit_masked_swizzle(ctx, bld, src, mask, allow_fi);
         emit_extract_vector(ctx, tmp, 0, dst);
      } else if (dst.regClass() == v2b) {
         Temp tmp = emit_masked_swizzle(ctx, bld, src, mask, allow_fi);
         emit_extract_vector(ctx, tmp, 0, dst);
      } else if (dst.regClass() == v1) {
         bld.copy(Definition(dst), emit_masked_swizzle(ctx, bld, src, mask, allow_fi));
      } else if (dst.regClass() == v2) {
         Temp lo = bld.tmp(v1), hi = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lo), Definition(hi), src);
         lo = emit_masked_swizzle(ctx, bld, lo, mask, allow_fi);
         hi = emit_masked_swizzle(ctx, bld, hi, mask, allow_fi);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lo, hi);
         emit_split_vector(ctx, dst, 2);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      set_wqm(ctx);
      break;
   }
   case nir_intrinsic_write_invocation_amd: {
      Temp src = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[0].ssa));
      Temp val = bld.as_uniform(get_ssa_temp(ctx, instr->src[1].ssa));
      Temp lane = bld.as_uniform(get_ssa_temp(ctx, instr->src[2].ssa));
      Temp dst = get_ssa_temp(ctx, &instr->def);
      if (dst.regClass() == v1) {
         /* src2 is ignored for writelane. RA assigns the same reg for dst */
         bld.writelane(Definition(dst), val, lane, src);
      } else if (dst.regClass() == v2) {
         Temp src_lo = bld.tmp(v1), src_hi = bld.tmp(v1);
         Temp val_lo = bld.tmp(s1), val_hi = bld.tmp(s1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(src_lo), Definition(src_hi), src);
         bld.pseudo(aco_opcode::p_split_vector, Definition(val_lo), Definition(val_hi), val);
         Temp lo = bld.writelane(bld.def(v1), val_lo, lane, src_hi);
         Temp hi = bld.writelane(bld.def(v1), val_hi, lane, src_hi);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lo, hi);
         emit_split_vector(ctx, dst, 2);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_intrinsic_mbcnt_amd: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      Temp add_src = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[1].ssa));
      Temp dst = get_ssa_temp(ctx, &instr->def);
      /* Fit 64-bit mask for wave32 */
      src = emit_extract_vector(ctx, src, 0, RegClass(src.type(), bld.lm.size()));
      emit_mbcnt(ctx, dst, Operand(src), Operand(add_src));
      set_wqm(ctx);
      break;
   }
   case nir_intrinsic_lane_permute_16_amd: {
      /* NOTE: If we use divergence analysis information here instead of the src regclass,
       * skip_uniformize_merge_phi() should be updated.
       */
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      Temp dst = get_ssa_temp(ctx, &instr->def);
      assert(ctx->program->gfx_level >= GFX10);

      if (src.regClass() == s1) {
         bld.copy(Definition(dst), src);
      } else if (dst.type() == RegType::vgpr && src.type() == RegType::vgpr && dst.size() == 1 &&
                 src.size() == 1) {
         bld.vop3(aco_opcode::v_permlane16_b32, Definition(dst), src,
                  bld.as_uniform(get_ssa_temp(ctx, instr->src[1].ssa)),
                  bld.as_uniform(get_ssa_temp(ctx, instr->src[2].ssa)));
      } else {
         isel_err(&instr->instr, "Unimplemented lane_permute_16_amd");
      }
      break;
   }
   case nir_intrinsic_load_helper_invocation:
   case nir_intrinsic_is_helper_invocation: {
      /* load_helper() after demote() get lowered to is_helper().
       * Otherwise, these two behave the same. */
      Temp dst = get_ssa_temp(ctx, &instr->def);
      bld.pseudo(aco_opcode::p_is_helper, Definition(dst), Operand(exec, bld.lm));
      ctx->program->needs_exact = true;
      break;
   }
   case nir_intrinsic_demote:
   case nir_intrinsic_demote_if: {
      Operand cond = Operand::c32(-1u);
      if (instr->intrinsic == nir_intrinsic_demote_if) {
         Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
         assert(src.regClass() == bld.lm);
         if (ctx->cf_info.in_divergent_cf) {
            cond = bld.sop2(Builder::s_and, bld.def(bld.lm), bld.def(s1, scc), src,
                            Operand(exec, bld.lm));
         } else {
            cond = Operand(src);
         }
      }

      bld.pseudo(aco_opcode::p_demote_to_helper, cond);

      /* Perform the demote in WQM so that it doesn't make exec empty.
       * WQM should last until at least the next top-level block.
       */
      if (ctx->cf_info.in_divergent_cf)
         set_wqm(ctx, true);

      ctx->block->kind |= block_kind_uses_discard;
      ctx->program->needs_exact = true;

      /* Enable WQM in order to prevent helper lanes from getting terminated. */
      if (ctx->shader->info.maximally_reconverges)
         ctx->program->needs_wqm = true;

      break;
   }
   case nir_intrinsic_terminate:
   case nir_intrinsic_terminate_if: {
      assert(ctx->cf_info.parent_loop.exit == NULL && "Terminate must not appear in loops.");
      Operand cond = Operand::c32(-1u);
      if (instr->intrinsic == nir_intrinsic_terminate_if) {
         Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
         assert(src.regClass() == bld.lm);
         if (ctx->cf_info.in_divergent_cf) {
            cond = bld.sop2(Builder::s_and, bld.def(bld.lm), bld.def(s1, scc), src,
                            Operand(exec, bld.lm));
         } else {
            cond = Operand(src);
         }

         ctx->cf_info.had_divergent_discard |= nir_src_is_divergent(&instr->src[0]);
      }

      bld.pseudo(aco_opcode::p_discard_if, cond);
      ctx->block->kind |= block_kind_uses_discard;

      if (ctx->cf_info.in_divergent_cf) {
         ctx->cf_info.exec.potentially_empty_discard = true;
         ctx->cf_info.had_divergent_discard = true;
         begin_empty_exec_skip(ctx, &instr->instr, instr->instr.block);
      }
      ctx->program->needs_exact = true;
      break;
   }
   case nir_intrinsic_debug_break: {
      bld.sopp(aco_opcode::s_trap, 1u);
      break;
   }
   case nir_intrinsic_first_invocation: {
      bld.sop1(Builder::s_ff1_i32, Definition(get_ssa_temp(ctx, &instr->def)),
               Operand(exec, bld.lm));
      set_wqm(ctx);
      break;
   }
   case nir_intrinsic_last_invocation: {
      Temp flbit = bld.sop1(Builder::s_flbit_i32, bld.def(s1), Operand(exec, bld.lm));
      bld.sop2(aco_opcode::s_sub_i32, Definition(get_ssa_temp(ctx, &instr->def)), bld.def(s1, scc),
               Operand::c32(ctx->program->wave_size - 1u), flbit);
      set_wqm(ctx);
      break;
   }
   case nir_intrinsic_elect: {
      /* p_elect is lowered in aco_insert_exec_mask.
       * Use exec as an operand so value numbering and the pre-RA optimizer won't recognize
       * two p_elect with different exec masks as the same.
       */
      bld.pseudo(aco_opcode::p_elect, Definition(get_ssa_temp(ctx, &instr->def)),
                 Operand(exec, bld.lm));
      set_wqm(ctx);
      break;
   }
   case nir_intrinsic_shader_clock: {
      Temp dst = get_ssa_temp(ctx, &instr->def);
      if (nir_intrinsic_memory_scope(instr) == SCOPE_SUBGROUP && ctx->options->gfx_level >= GFX12) {
         Temp hi0 = bld.tmp(s1);
         Temp hi1 = bld.tmp(s1);
         Temp lo = bld.tmp(s1);
         bld.pseudo(aco_opcode::p_shader_cycles_hi_lo_hi, Definition(hi0), Definition(lo),
                    Definition(hi1));
         Temp hi_eq = bld.sopc(aco_opcode::s_cmp_eq_u32, bld.def(s1, scc), hi0, hi1);
         lo = bld.sop2(aco_opcode::s_cselect_b32, bld.def(s1), lo, Operand::zero(), bld.scc(hi_eq));
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lo, hi1);
      } else if (nir_intrinsic_memory_scope(instr) == SCOPE_SUBGROUP &&
                 ctx->options->gfx_level >= GFX10_3) {
         /* "((size - 1) << 11) | register" (SHADER_CYCLES is encoded as register 29) */
         Temp clock = bld.sopk(aco_opcode::s_getreg_b32, bld.def(s1), ((20 - 1) << 11) | 29);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), clock, Operand::zero());
      } else if (nir_intrinsic_memory_scope(instr) == SCOPE_DEVICE &&
                 ctx->options->gfx_level >= GFX11) {
         bld.sop1(aco_opcode::s_sendmsg_rtn_b64, Definition(dst),
                  Operand::c32(sendmsg_rtn_get_realtime));
      } else {
         aco_opcode opcode = nir_intrinsic_memory_scope(instr) == SCOPE_DEVICE
                                ? aco_opcode::s_memrealtime
                                : aco_opcode::s_memtime;
         bld.smem(opcode, Definition(dst), memory_sync_info(0, semantic_volatile));
      }
      emit_split_vector(ctx, dst, 2);
      break;
   }
   case nir_intrinsic_sendmsg_amd: {
      unsigned imm = nir_intrinsic_base(instr);
      Temp m0_content = bld.as_uniform(get_ssa_temp(ctx, instr->src[0].ssa));
      bld.sopp(aco_opcode::s_sendmsg, bld.m0(m0_content), imm);
      break;
   }
   case nir_intrinsic_is_subgroup_invocation_lt_amd: {
      Temp src = bld.as_uniform(get_ssa_temp(ctx, instr->src[0].ssa));
      unsigned offset = nir_intrinsic_base(instr);
      bld.copy(Definition(get_ssa_temp(ctx, &instr->def)), lanecount_to_mask(ctx, src, offset));
      break;
   }
   case nir_intrinsic_gds_atomic_add_amd: {
      Temp store_val = get_ssa_temp(ctx, instr->src[0].ssa);
      Temp gds_addr = get_ssa_temp(ctx, instr->src[1].ssa);
      Temp m0_val = get_ssa_temp(ctx, instr->src[2].ssa);
      Operand m = bld.m0((Temp)bld.copy(bld.def(s1, m0), bld.as_uniform(m0_val)));
      bld.ds(aco_opcode::ds_add_u32, as_vgpr(ctx, gds_addr), as_vgpr(ctx, store_val), m, 0u, 0u,
             true);
      break;
   }
   case nir_intrinsic_load_sbt_base_amd: {
      Temp dst = get_ssa_temp(ctx, &instr->def);
      Temp addr = get_arg(ctx, ctx->args->rt.sbt_descriptors);
      assert(addr.regClass() == s2);
      bld.copy(Definition(dst), Operand(addr));
      break;
   }
   case nir_intrinsic_bvh64_intersect_ray_amd: visit_bvh64_intersect_ray_amd(ctx, instr); break;
   case nir_intrinsic_bvh8_intersect_ray_amd: visit_bvh8_intersect_ray_amd(ctx, instr); break;
   case nir_intrinsic_load_resume_shader_address_amd: {
      bld.pseudo(aco_opcode::p_resume_shader_address, Definition(get_ssa_temp(ctx, &instr->def)),
                 bld.def(s1, scc), Operand::c32(nir_intrinsic_call_idx(instr)));
      break;
   }
   case nir_intrinsic_load_scalar_arg_amd:
   case nir_intrinsic_load_vector_arg_amd: {
      assert(nir_intrinsic_base(instr) < ctx->args->arg_count);
      Temp dst = get_ssa_temp(ctx, &instr->def);
      Temp src = ctx->arg_temps[nir_intrinsic_base(instr)];
      assert(src.id());
      assert(src.type() == (instr->intrinsic == nir_intrinsic_load_scalar_arg_amd ? RegType::sgpr
                                                                                  : RegType::vgpr));
      bld.copy(Definition(dst), src);
      emit_split_vector(ctx, dst, dst.size());
      break;
   }
   case nir_intrinsic_ordered_xfb_counter_add_gfx11_amd: {
      Temp dst = get_ssa_temp(ctx, &instr->def);
      Temp ordered_id = get_ssa_temp(ctx, instr->src[0].ssa);
      Temp counter = get_ssa_temp(ctx, instr->src[1].ssa);

      Temp gds_base = bld.copy(bld.def(v1), Operand::c32(0u));
      unsigned offset0, offset1;
      Instruction* ds_instr;
      Operand m;

      /* Lock a GDS mutex. */
      ds_ordered_count_offsets(ctx, 1 << 24u, false, false, &offset0, &offset1);
      m = bld.m0(bld.as_uniform(ordered_id));
      ds_instr =
         bld.ds(aco_opcode::ds_ordered_count, bld.def(v1), gds_base, m, offset0, offset1, true);
      ds_instr->ds().sync = memory_sync_info(storage_gds, semantic_volatile);

      aco_ptr<Instruction> vec{
         create_instruction(aco_opcode::p_create_vector, Format::PSEUDO, instr->num_components, 1)};
      unsigned write_mask = nir_intrinsic_write_mask(instr);

      for (unsigned i = 0; i < instr->num_components; i++) {
         if (write_mask & (1 << i)) {
            Temp chan_counter = emit_extract_vector(ctx, counter, i, v1);

            ds_instr = bld.ds(aco_opcode::ds_add_gs_reg_rtn, bld.def(v1), Operand(), chan_counter,
                              i * 4, 0u, true);
            ds_instr->ds().sync = memory_sync_info(storage_gds, semantic_atomicrmw);

            vec->operands[i] = Operand(ds_instr->definitions[0].getTemp());
         } else {
            vec->operands[i] = Operand::zero();
         }
      }

      vec->definitions[0] = Definition(dst);
      ctx->block->instructions.emplace_back(std::move(vec));

      /* Unlock a GDS mutex. */
      ds_ordered_count_offsets(ctx, 1 << 24u, true, true, &offset0, &offset1);
      m = bld.m0(bld.as_uniform(ordered_id));
      ds_instr =
         bld.ds(aco_opcode::ds_ordered_count, bld.def(v1), gds_base, m, offset0, offset1, true);
      ds_instr->ds().sync = memory_sync_info(storage_gds, semantic_volatile);

      emit_split_vector(ctx, dst, instr->num_components);
      break;
   }
   case nir_intrinsic_xfb_counter_sub_gfx11_amd: {
      unsigned write_mask = nir_intrinsic_write_mask(instr);
      Temp counter = get_ssa_temp(ctx, instr->src[0].ssa);

      u_foreach_bit (i, write_mask) {
         Temp chan_counter = emit_extract_vector(ctx, counter, i, v1);
         Instruction* ds_instr;

         ds_instr = bld.ds(aco_opcode::ds_sub_gs_reg_rtn, bld.def(v1), Operand(), chan_counter,
                           i * 4, 0u, true);
         ds_instr->ds().sync = memory_sync_info(storage_gds, semantic_atomicrmw);
      }
      break;
   }
   case nir_intrinsic_export_amd:
   case nir_intrinsic_export_row_amd: {
      unsigned flags = nir_intrinsic_flags(instr);
      unsigned target = nir_intrinsic_base(instr);
      unsigned write_mask = nir_intrinsic_write_mask(instr);

      /* Mark vertex export block. */
      if (target == V_008DFC_SQ_EXP_POS || target <= V_008DFC_SQ_EXP_NULL)
         ctx->block->kind |= block_kind_export_end;

      if (target < V_008DFC_SQ_EXP_MRTZ)
         ctx->program->has_color_exports = true;

      const bool row_en = instr->intrinsic == nir_intrinsic_export_row_amd;

      aco_ptr<Instruction> exp{create_instruction(aco_opcode::exp, Format::EXP, 6 + row_en, 0)};

      exp->exp().dest = target;
      exp->exp().enabled_mask = write_mask;
      exp->exp().compressed = flags & AC_EXP_FLAG_COMPRESSED;

      /* ACO may reorder position/mrt export instructions, then mark done for last
       * export instruction. So don't respect the nir AC_EXP_FLAG_DONE for position/mrt
       * exports here and leave it to ACO.
       */
      if (target == V_008DFC_SQ_EXP_PRIM)
         exp->exp().done = flags & AC_EXP_FLAG_DONE;
      else
         exp->exp().done = false;

      /* ACO may reorder mrt export instructions, then mark valid mask for last
       * export instruction. So don't respect the nir AC_EXP_FLAG_VALID_MASK for mrt
       * exports here and leave it to ACO.
       */
      if (target > V_008DFC_SQ_EXP_NULL)
         exp->exp().valid_mask = flags & AC_EXP_FLAG_VALID_MASK;
      else
         exp->exp().valid_mask = false;

      exp->exp().row_en = row_en;

      /* Compressed export uses two bits for a channel. */
      uint32_t channel_mask = exp->exp().compressed
                                 ? (write_mask & 0x3 ? 1 : 0) | (write_mask & 0xc ? 2 : 0)
                                 : write_mask;

      Temp value = get_ssa_temp(ctx, instr->src[0].ssa);
      for (unsigned i = 0; i < 4; i++) {
         exp->operands[i] = channel_mask & BITFIELD_BIT(i)
                               ? Operand(emit_extract_vector(ctx, value, i, v1))
                               : Operand(v1);
      }

      if (row_en) {
         Temp row = bld.as_uniform(get_ssa_temp(ctx, instr->src[1].ssa));
         /* Hack to prevent the RA from moving the source into m0 and then back to a normal SGPR. */
         row = bld.copy(bld.def(s1, m0), row);
         exp->operands[4] = bld.m0(row);
      }

      exp->exp().disable_wqm = true;
      instr_exact_mask(exp.get()) = Operand();
      instr_wqm_mask(exp.get()) = Operand();

      ctx->block->instructions.emplace_back(std::move(exp));
      break;
   }
   case nir_intrinsic_export_dual_src_blend_amd: {
      Temp val0 = get_ssa_temp(ctx, instr->src[0].ssa);
      Temp val1 = get_ssa_temp(ctx, instr->src[1].ssa);
      unsigned write_mask = nir_intrinsic_write_mask(instr);

      struct aco_export_mrt mrt0, mrt1;
      for (unsigned i = 0; i < 4; i++) {
         mrt0.out[i] = write_mask & BITFIELD_BIT(i) ? Operand(emit_extract_vector(ctx, val0, i, v1))
                                                    : Operand(v1);

         mrt1.out[i] = write_mask & BITFIELD_BIT(i) ? Operand(emit_extract_vector(ctx, val1, i, v1))
                                                    : Operand(v1);
      }
      mrt0.enabled_channels = mrt1.enabled_channels = write_mask;

      create_fs_dual_src_export_gfx11(ctx, &mrt0, &mrt1);

      ctx->block->kind |= block_kind_export_end;
      break;
   }
   case nir_intrinsic_strict_wqm_coord_amd: {
      Temp dst = get_ssa_temp(ctx, &instr->def);
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      unsigned begin_size = nir_intrinsic_base(instr);

      unsigned num_src = 1;
      auto it = ctx->allocated_vec.find(src.id());
      if (it != ctx->allocated_vec.end())
         num_src = src.bytes() / it->second[0].bytes();

      aco_ptr<Instruction> vec{create_instruction(aco_opcode::p_start_linear_vgpr, Format::PSEUDO,
                                                  num_src + !!begin_size, 1)};

      if (begin_size)
         vec->operands[0] = Operand(RegClass::get(RegType::vgpr, begin_size));
      for (unsigned i = 0; i < num_src; i++) {
         Temp comp = it != ctx->allocated_vec.end() ? it->second[i] : src;
         vec->operands[i + !!begin_size] = Operand(comp);
      }

      vec->definitions[0] = Definition(dst);
      ctx->block->instructions.emplace_back(std::move(vec));
      set_wqm(ctx, true);
      break;
   }
   case nir_intrinsic_store_scalar_arg_amd: {
      BITSET_SET(ctx->output_args, nir_intrinsic_base(instr));
      ctx->arg_temps[nir_intrinsic_base(instr)] =
         bld.as_uniform(get_ssa_temp(ctx, instr->src[0].ssa));
      break;
   }
   case nir_intrinsic_store_vector_arg_amd: {
      BITSET_SET(ctx->output_args, nir_intrinsic_base(instr));
      ctx->arg_temps[nir_intrinsic_base(instr)] =
         as_vgpr(ctx, get_ssa_temp(ctx, instr->src[0].ssa));
      break;
   }
   case nir_intrinsic_begin_invocation_interlock: {
      pops_await_overlapped_waves(ctx);
      break;
   }
   case nir_intrinsic_end_invocation_interlock: {
      /* The `done` export exits the POPS ordered section on GFX11+. */
      if (ctx->options->gfx_level < GFX11)
         bld.pseudo(aco_opcode::p_pops_gfx9_ordered_section_done);
      break;
   }
   case nir_intrinsic_cmat_muladd_amd: visit_cmat_muladd(ctx, instr); break;
   case nir_intrinsic_nop_amd: bld.sopp(aco_opcode::s_nop, nir_intrinsic_base(instr)); break;
   case nir_intrinsic_sleep_amd: bld.sopp(aco_opcode::s_sleep, nir_intrinsic_base(instr)); break;
   case nir_intrinsic_unit_test_amd:
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(nir_intrinsic_base(instr)),
                 get_ssa_temp(ctx, instr->src[0].ssa));
      break;
   case nir_intrinsic_unit_test_uniform_amd:
   case nir_intrinsic_unit_test_divergent_amd:
      bld.pseudo(aco_opcode::p_unit_test, Definition(get_ssa_temp(ctx, &instr->def)),
                 Operand::c32(nir_intrinsic_base(instr)));
      break;
   case nir_intrinsic_bvh_stack_rtn_amd: {
      switch (instr->num_components) {
      case 4: emit_ds_bvh_stack_push4_pop1_rtn(ctx, instr, bld); break;
      case 8: emit_ds_bvh_stack_push8_pop1_rtn(ctx, instr, bld); break;
      default: UNREACHABLE("Invalid BVH stack component count!");
      }
      break;
   }
   default:
      isel_err(&instr->instr, "Unimplemented intrinsic instr");
      abort();

      break;
   }
}

} // namespace aco
