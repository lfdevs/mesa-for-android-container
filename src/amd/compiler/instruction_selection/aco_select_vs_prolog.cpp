/*
 * Copyright © 2021 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "aco_builder.h"
#include "aco_instruction_selection.h"
#include "aco_ir.h"

#include "amdgfxregs.h"

namespace aco {
namespace {

Operand
get_arg_fixed(const struct ac_shader_args* args, struct ac_arg arg)
{
   enum ac_arg_regfile file = args->args[arg.arg_index].file;
   unsigned size = args->args[arg.arg_index].size;
   RegClass rc = RegClass(file == AC_ARG_SGPR ? RegType::sgpr : RegType::vgpr, size);
   return Operand(get_arg_reg(args, arg), rc);
}

unsigned
load_vb_descs(Builder& bld, PhysReg dest, Operand base, unsigned start, unsigned max)
{
   unsigned sgpr_limit = get_addr_regs_from_waves(bld.program, bld.program->min_waves).sgpr;
   unsigned count = MIN2((sgpr_limit - dest.reg()) / 4u, max);
   for (unsigned i = 0; i < count;) {
      unsigned size = 1u << util_logbase2(MIN2(count - i, 4));

      if (size == 4)
         bld.smem(aco_opcode::s_load_dwordx16, Definition(dest, s16), base,
                  Operand::c32((start + i) * 16u));
      else if (size == 2)
         bld.smem(aco_opcode::s_load_dwordx8, Definition(dest, s8), base,
                  Operand::c32((start + i) * 16u));
      else
         bld.smem(aco_opcode::s_load_dwordx4, Definition(dest, s4), base,
                  Operand::c32((start + i) * 16u));

      dest = dest.advance(size * 16u);
      i += size;
   }

   return count;
}

void
wait_for_smem_loads(Builder& bld)
{
   if (bld.program->gfx_level >= GFX12) {
      bld.sopp(aco_opcode::s_wait_kmcnt, 0);
   } else {
      wait_imm lgkm_imm;
      lgkm_imm.lgkm = 0;
      bld.sopp(aco_opcode::s_waitcnt, lgkm_imm.pack(bld.program->gfx_level));
   }
}

void
wait_for_vmem_loads(Builder& bld)
{
   if (bld.program->gfx_level >= GFX12) {
      bld.sopp(aco_opcode::s_wait_loadcnt, 0);
   } else {
      wait_imm vm_imm;
      vm_imm.vm = 0;
      bld.sopp(aco_opcode::s_waitcnt, vm_imm.pack(bld.program->gfx_level));
   }
}

Operand
calc_nontrivial_instance_id(Builder& bld, const struct ac_shader_args* args,
                            const struct aco_vs_prolog_info* pinfo, unsigned index,
                            Operand instance_id, Operand start_instance, PhysReg tmp_sgpr,
                            PhysReg tmp_vgpr0, PhysReg tmp_vgpr1)
{
   bld.smem(aco_opcode::s_load_dwordx2, Definition(tmp_sgpr, s2),
            get_arg_fixed(args, pinfo->inputs), Operand::c32(8u + index * 8u));

   wait_for_smem_loads(bld);

   Definition fetch_index_def(tmp_vgpr0, v1);
   Operand fetch_index(tmp_vgpr0, v1);

   Operand div_info(tmp_sgpr, s1);
   if (bld.program->gfx_level >= GFX8 && bld.program->gfx_level < GFX11) {
      /* use SDWA */
      if (bld.program->gfx_level < GFX9) {
         bld.vop1(aco_opcode::v_mov_b32, Definition(tmp_vgpr1, v1), div_info);
         div_info = Operand(tmp_vgpr1, v1);
      }

      bld.vop2(aco_opcode::v_lshrrev_b32, fetch_index_def, div_info, instance_id);

      Instruction* instr;
      if (bld.program->gfx_level >= GFX9)
         instr = bld.vop2_sdwa(aco_opcode::v_add_u32, fetch_index_def, div_info, fetch_index).instr;
      else
         instr = bld.vop2_sdwa(aco_opcode::v_add_co_u32, fetch_index_def, Definition(vcc, bld.lm),
                               div_info, fetch_index)
                    .instr;
      instr->sdwa().sel[0] = SubdwordSel::ubyte1;

      bld.vop3(aco_opcode::v_mul_hi_u32, fetch_index_def, Operand(tmp_sgpr.advance(4), s1),
               fetch_index);

      instr =
         bld.vop2_sdwa(aco_opcode::v_lshrrev_b32, fetch_index_def, div_info, fetch_index).instr;
      instr->sdwa().sel[0] = SubdwordSel::ubyte2;
   } else {
      Operand tmp_op(tmp_vgpr1, v1);
      Definition tmp_def(tmp_vgpr1, v1);

      bld.vop2(aco_opcode::v_lshrrev_b32, fetch_index_def, div_info, instance_id);

      bld.vop3(aco_opcode::v_bfe_u32, tmp_def, div_info, Operand::c32(8u), Operand::c32(8u));
      bld.vadd32(fetch_index_def, tmp_op, fetch_index, false, Operand(s2), true);

      bld.vop3(aco_opcode::v_mul_hi_u32, fetch_index_def, fetch_index,
               Operand(tmp_sgpr.advance(4), s1));

      bld.vop3(aco_opcode::v_bfe_u32, tmp_def, div_info, Operand::c32(16u), Operand::c32(8u));
      bld.vop2(aco_opcode::v_lshrrev_b32, fetch_index_def, tmp_op, fetch_index);
   }

   bld.vadd32(fetch_index_def, start_instance, fetch_index, false, Operand(s2), true);

   return fetch_index;
}

PhysReg
get_next_vgpr(unsigned size, unsigned* num, int* offset = NULL)
{
   unsigned reg = *num + (offset ? *offset : 0);
   if (reg + size >= *num) {
      *num = reg + size;
      if (offset)
         *offset = 0;
   } else if (offset) {
      *offset += size;
   }
   return PhysReg(256 + reg);
}

struct UnalignedVsAttribLoad {
   /* dst/scratch are PhysReg converted to unsigned */
   unsigned dst;
   unsigned scratch;
   bool d16;
   const struct ac_vtx_format_info* vtx_info;
};

struct UnalignedVsAttribLoadState {
   unsigned max_vgprs;
   unsigned initial_num_vgprs;
   unsigned* num_vgprs;
   unsigned overflow_num_vgprs;
   aco::small_vec<UnalignedVsAttribLoad, 16> current_loads;
};

void
convert_unaligned_vs_attrib(Builder& bld, UnalignedVsAttribLoad load)
{
   PhysReg dst(load.dst);
   PhysReg scratch(load.scratch);
   const struct ac_vtx_format_info* vtx_info = load.vtx_info;
   unsigned dfmt = vtx_info->hw_format[0] & 0xf;
   unsigned nfmt = vtx_info->hw_format[0] >> 4;

   unsigned size = vtx_info->chan_byte_size ? vtx_info->chan_byte_size : vtx_info->element_size;
   if (load.d16) {
      bld.vop3(aco_opcode::v_lshl_or_b32, Definition(dst, v1), Operand(scratch, v1),
               Operand::c32(8), Operand(dst, v1));
   } else {
      for (unsigned i = 1; i < size; i++) {
         PhysReg byte_reg = scratch.advance(i * 4 - 4);
         if (bld.program->gfx_level >= GFX9) {
            bld.vop3(aco_opcode::v_lshl_or_b32, Definition(dst, v1), Operand(byte_reg, v1),
                     Operand::c32(i * 8), Operand(dst, v1));
         } else {
            bld.vop2(aco_opcode::v_lshlrev_b32, Definition(byte_reg, v1), Operand::c32(i * 8),
                     Operand(byte_reg, v1));
            bld.vop2(aco_opcode::v_or_b32, Definition(dst, v1), Operand(dst, v1),
                     Operand(byte_reg, v1));
         }
      }
   }

   unsigned num_channels = vtx_info->chan_byte_size ? 1 : vtx_info->num_channels;
   PhysReg chan[4] = {dst, dst.advance(4), dst.advance(8), dst.advance(12)};

   if (dfmt == V_008F0C_BUF_DATA_FORMAT_10_11_11) {
      bld.vop3(aco_opcode::v_bfe_u32, Definition(chan[2], v1), Operand(dst, v1), Operand::c32(22),
               Operand::c32(10));
      bld.vop3(aco_opcode::v_bfe_u32, Definition(chan[1], v1), Operand(dst, v1), Operand::c32(11),
               Operand::c32(11));
      bld.vop3(aco_opcode::v_bfe_u32, Definition(chan[0], v1), Operand(dst, v1), Operand::c32(0),
               Operand::c32(11));
      bld.vop2(aco_opcode::v_lshlrev_b32, Definition(chan[2], v1), Operand::c32(5),
               Operand(chan[2], v1));
      bld.vop2(aco_opcode::v_lshlrev_b32, Definition(chan[1], v1), Operand::c32(4),
               Operand(chan[1], v1));
      bld.vop2(aco_opcode::v_lshlrev_b32, Definition(chan[0], v1), Operand::c32(4),
               Operand(chan[0], v1));
   } else if (dfmt == V_008F0C_BUF_DATA_FORMAT_2_10_10_10) {
      aco_opcode bfe = aco_opcode::v_bfe_u32;
      switch (nfmt) {
      case V_008F0C_BUF_NUM_FORMAT_SNORM:
      case V_008F0C_BUF_NUM_FORMAT_SSCALED:
      case V_008F0C_BUF_NUM_FORMAT_SINT: bfe = aco_opcode::v_bfe_i32; break;
      default: break;
      }

      bool swapxz = G_008F0C_DST_SEL_X(vtx_info->dst_sel) != V_008F0C_SQ_SEL_X;
      bld.vop3(bfe, Definition(chan[3], v1), Operand(dst, v1), Operand::c32(30), Operand::c32(2));
      bld.vop3(bfe, Definition(chan[2], v1), Operand(dst, v1), Operand::c32(swapxz ? 0 : 20),
               Operand::c32(10));
      bld.vop3(bfe, Definition(chan[1], v1), Operand(dst, v1), Operand::c32(10), Operand::c32(10));
      bld.vop3(bfe, Definition(chan[0], v1), Operand(dst, v1), Operand::c32(swapxz ? 20 : 0),
               Operand::c32(10));
   } else if (dfmt == V_008F0C_BUF_DATA_FORMAT_8 || dfmt == V_008F0C_BUF_DATA_FORMAT_16) {
      unsigned bits = dfmt == V_008F0C_BUF_DATA_FORMAT_8 ? 8 : 16;
      switch (nfmt) {
      case V_008F0C_BUF_NUM_FORMAT_SNORM:
      case V_008F0C_BUF_NUM_FORMAT_SSCALED:
      case V_008F0C_BUF_NUM_FORMAT_SINT:
         bld.vop3(aco_opcode::v_bfe_i32, Definition(dst, v1), Operand(dst, v1), Operand::c32(0),
                  Operand::c32(bits));
         break;
      default: break;
      }
   }

   if (nfmt == V_008F0C_BUF_NUM_FORMAT_FLOAT &&
       (dfmt == V_008F0C_BUF_DATA_FORMAT_16 || dfmt == V_008F0C_BUF_DATA_FORMAT_10_11_11)) {
      for (unsigned i = 0; i < num_channels; i++)
         bld.vop1(aco_opcode::v_cvt_f32_f16, Definition(chan[i], v1), Operand(chan[i], v1));
   } else if (nfmt == V_008F0C_BUF_NUM_FORMAT_USCALED || nfmt == V_008F0C_BUF_NUM_FORMAT_UNORM) {
      for (unsigned i = 0; i < num_channels; i++)
         bld.vop1(aco_opcode::v_cvt_f32_u32, Definition(chan[i], v1), Operand(chan[i], v1));
   } else if (nfmt == V_008F0C_BUF_NUM_FORMAT_SSCALED || nfmt == V_008F0C_BUF_NUM_FORMAT_SNORM) {
      for (unsigned i = 0; i < num_channels; i++)
         bld.vop1(aco_opcode::v_cvt_f32_i32, Definition(chan[i], v1), Operand(chan[i], v1));
   }

   std::array<unsigned, 4> chan_max;
   switch (dfmt) {
   case V_008F0C_BUF_DATA_FORMAT_2_10_10_10: chan_max = {1023, 1023, 1023, 3}; break;
   case V_008F0C_BUF_DATA_FORMAT_8: chan_max = {255, 255, 255, 255}; break;
   case V_008F0C_BUF_DATA_FORMAT_16: chan_max = {65535, 65535, 65535, 65535}; break;
   }

   if (nfmt == V_008F0C_BUF_NUM_FORMAT_UNORM) {
      for (unsigned i = 0; i < num_channels; i++)
         bld.vop2(aco_opcode::v_mul_f32, Definition(chan[i], v1),
                  Operand::c32(fui(1.0 / chan_max[i])), Operand(chan[i], v1));
   } else if (nfmt == V_008F0C_BUF_NUM_FORMAT_SNORM) {
      for (unsigned i = 0; i < num_channels; i++) {
         bld.vop2(aco_opcode::v_mul_f32, Definition(chan[i], v1),
                  Operand::c32(fui(1.0 / (chan_max[i] >> 1))), Operand(chan[i], v1));
         bld.vop2(aco_opcode::v_max_f32, Definition(chan[i], v1), Operand::c32(0xbf800000),
                  Operand(chan[i], v1));
      }
   }
}

void
convert_current_unaligned_vs_attribs(Builder& bld, UnalignedVsAttribLoadState* state)
{
   if (state->current_loads.empty())
      return;

   wait_for_vmem_loads(bld);

   for (UnalignedVsAttribLoad load : state->current_loads)
      convert_unaligned_vs_attrib(bld, load);
   state->current_loads.clear();

   state->overflow_num_vgprs = state->initial_num_vgprs;
   state->num_vgprs = &state->overflow_num_vgprs;
}

void
load_unaligned_vs_attrib(Builder& bld, PhysReg dst, Operand desc, Operand index, uint32_t offset,
                         const struct ac_vtx_format_info* vtx_info,
                         UnalignedVsAttribLoadState* state)
{
   unsigned size = vtx_info->chan_byte_size ? vtx_info->chan_byte_size : vtx_info->element_size;

   UnalignedVsAttribLoad load;
   load.dst = dst;
   load.vtx_info = vtx_info;
   load.d16 = bld.program->gfx_level >= GFX9 && !bld.program->dev.sram_ecc_enabled && size == 4;

   unsigned num_scratch_vgprs = load.d16 ? 1 : (size - 1);
   if (!vtx_info->chan_byte_size) {
      /* When chan_byte_size==0, we're loading the entire attribute, so we can use the last 3
       * components of the destination.
       */
      assert(num_scratch_vgprs <= 3);
      load.scratch = dst.advance(4);
   } else {
      if (*state->num_vgprs + num_scratch_vgprs > state->max_vgprs)
         convert_current_unaligned_vs_attribs(bld, state);

      load.scratch = get_next_vgpr(num_scratch_vgprs, state->num_vgprs, NULL);
   }

   PhysReg scratch(load.scratch);
   if (load.d16) {
      bld.mubuf(aco_opcode::buffer_load_ubyte_d16, Definition(dst, v1), desc, index,
                Operand::c32(0u), offset, false, true);
      bld.mubuf(aco_opcode::buffer_load_ubyte_d16_hi, Definition(dst, v1), desc, index,
                Operand::c32(0u), offset + 2, false, true);
      bld.mubuf(aco_opcode::buffer_load_ubyte_d16, Definition(scratch, v1), desc, index,
                Operand::c32(0u), offset + 1, false, true);
      bld.mubuf(aco_opcode::buffer_load_ubyte_d16_hi, Definition(scratch, v1), desc, index,
                Operand::c32(0u), offset + 3, false, true);
   } else {
      for (unsigned i = 0; i < size; i++) {
         Definition def(i ? scratch.advance(i * 4 - 4) : dst, v1);
         unsigned soffset = 0, const_offset = 0;

         if (bld.program->gfx_level >= GFX12) {
            const_offset = offset + i;
         } else {
            soffset = offset + i;
         }

         bld.mubuf(aco_opcode::buffer_load_ubyte, def, desc, index, Operand::c32(soffset),
                   const_offset, false, true);
      }
   }

   state->current_loads.push_back(load);
}

bool
is_last_attribute_large(const struct aco_vs_prolog_info* pinfo)
{
   const struct ac_vtx_format_info* vtx_info_table =
      ac_get_vtx_format_info_table(GFX8, CHIP_POLARIS10);
   unsigned last_attribute = pinfo->num_attributes - 1;

   if ((pinfo->misaligned_mask & (1u << last_attribute))) {
      const struct ac_vtx_format_info* vtx_info = &vtx_info_table[pinfo->formats[last_attribute]];
      if (vtx_info->chan_byte_size == 8 && vtx_info->num_channels > 2)
         return true;
   }

   return false;
}

} // namespace

void
select_vs_prolog(Program* program, const struct aco_vs_prolog_info* pinfo, ac_shader_config* config,
                 const struct aco_compiler_options* options, const struct aco_shader_info* info,
                 const struct ac_shader_args* args)
{
   assert(pinfo->num_attributes > 0);

   /* This should be enough for any shader/stage. */
   unsigned max_user_sgprs = options->gfx_level >= GFX9 ? 32 : 16;

   init_program(program, compute_cs, info, options->gfx_level, options->family, options->wgp_mode,
                config);
   program->dev.vgpr_limit = 256;

   Block* block = program->create_and_insert_block();
   block->kind = block_kind_top_level;

   program->workgroup_size = 64;
   calc_min_waves(program);

   /* Addition on GFX6-8 requires a carry-out (we use VCC) */
   program->needs_vcc = program->gfx_level <= GFX8;

   Builder bld(program, block);

   block->instructions.reserve(16 + pinfo->num_attributes * 4);

   /* Besides performance, the purpose of this is also for the FeatureRequiredExportPriority GFX11.5
    * issue. */
   bld.sopp(aco_opcode::s_setprio, 3);

   uint32_t attrib_mask = BITFIELD_MASK(pinfo->num_attributes);
   bool has_nontrivial_divisors = pinfo->nontrivial_divisors;

   /* choose sgprs */
   PhysReg vertex_buffers(align(max_user_sgprs + 14, 2));
   PhysReg prolog_input = vertex_buffers.advance(8);
   PhysReg desc(
      align((has_nontrivial_divisors ? prolog_input : vertex_buffers).advance(8).reg(), 4));

   Operand start_instance = get_arg_fixed(args, args->start_instance);
   Operand instance_id = get_arg_fixed(args, args->instance_id);

   bool needs_instance_index =
      pinfo->instance_rate_inputs &
      ~(pinfo->zero_divisors | pinfo->nontrivial_divisors); /* divisor is 1 */
   bool needs_start_instance = pinfo->instance_rate_inputs & pinfo->zero_divisors;
   bool needs_vertex_index = ~pinfo->instance_rate_inputs & attrib_mask;
   bool needs_tmp_vgpr0 = has_nontrivial_divisors;
   bool needs_tmp_vgpr1 =
      has_nontrivial_divisors && (program->gfx_level <= GFX8 || program->gfx_level >= GFX11);

   int vgpr_offset = pinfo->misaligned_mask & (1u << (pinfo->num_attributes - 1)) ? 0 : -4;
   const bool is_last_attr_large = is_last_attribute_large(pinfo);

   unsigned num_vgprs = args->num_vgprs_used;
   PhysReg attributes_start =
      get_next_vgpr(pinfo->num_attributes * 4 + (is_last_attr_large ? 4 : 0), &num_vgprs);
   PhysReg vertex_index, instance_index, start_instance_vgpr, nontrivial_tmp_vgpr0,
      nontrivial_tmp_vgpr1;
   if (needs_vertex_index)
      vertex_index = get_next_vgpr(1, &num_vgprs, &vgpr_offset);
   if (needs_instance_index)
      instance_index = get_next_vgpr(1, &num_vgprs, &vgpr_offset);
   if (needs_start_instance)
      start_instance_vgpr = get_next_vgpr(1, &num_vgprs, &vgpr_offset);
   if (needs_tmp_vgpr0)
      nontrivial_tmp_vgpr0 = get_next_vgpr(1, &num_vgprs, &vgpr_offset);
   if (needs_tmp_vgpr1)
      nontrivial_tmp_vgpr1 = get_next_vgpr(1, &num_vgprs, &vgpr_offset);

   bld.sop1(aco_opcode::s_mov_b32, Definition(vertex_buffers, s1),
            get_arg_fixed(args, args->vertex_buffers));
   if (options->address32_hi >= 0xffff8000 || options->address32_hi <= 0x7fff) {
      bld.sopk(aco_opcode::s_movk_i32, Definition(vertex_buffers.advance(4), s1),
               options->address32_hi & 0xFFFF);
   } else {
      bld.sop1(aco_opcode::s_mov_b32, Definition(vertex_buffers.advance(4), s1),
               Operand::c32((unsigned)options->address32_hi));
   }

   const struct ac_vtx_format_info* vtx_info_table =
      ac_get_vtx_format_info_table(GFX8, CHIP_POLARIS10);

   UnalignedVsAttribLoadState unaligned_state;
   unaligned_state.max_vgprs = MAX2(84, num_vgprs + 8);
   unaligned_state.initial_num_vgprs = num_vgprs;
   unaligned_state.num_vgprs = &num_vgprs;

   unsigned num_sgprs = 0;
   for (unsigned loc = 0; loc < pinfo->num_attributes;) {
      unsigned num_descs =
         load_vb_descs(bld, desc, Operand(vertex_buffers, s2), loc, pinfo->num_attributes - loc);
      num_sgprs = MAX2(num_sgprs, desc.advance(num_descs * 16u).reg());

      if (loc == 0) {
         /* perform setup while we load the descriptors */
         if (pinfo->is_ngg || pinfo->next_stage != MESA_SHADER_VERTEX) {
            Operand count = get_arg_fixed(args, args->merged_wave_info);
            bld.sop2(aco_opcode::s_bfm_b64, Definition(exec, s2), count, Operand::c32(0u));
            if (program->wave_size == 64) {
               bld.sopc(aco_opcode::s_bitcmp1_b32, Definition(scc, s1), count,
                        Operand::c32(6u /* log2(64) */));
               bld.sop2(aco_opcode::s_cselect_b64, Definition(exec, s2), Operand::c64(UINT64_MAX),
                        Operand(exec, s2), Operand(scc, s1));
            }
         }

         /* If there are no HS threads, SPI mistakenly loads the LS VGPRs starting at VGPR 0. */
         if (info->hw_stage == AC_HW_HULL_SHADER && options->has_ls_vgpr_init_bug) {
            /* We don't want load_vb_descs() to write vcc. */
            assert(program->dev.sgpr_limit <= vcc.reg());

            bld.sop2(aco_opcode::s_bfe_u32, Definition(vcc, s1), Definition(scc, s1),
                     get_arg_fixed(args, args->merged_wave_info), Operand::c32((8u << 16) | 8u));
            bld.sop2(Builder::s_cselect, Definition(vcc, bld.lm), Operand::c32(-1), Operand::zero(),
                     Operand(scc, s1));

            /* These copies are ordered so that vertex_id=tcs_patch_id doesn't overwrite vertex_id
             * before instance_id=vertex_id. */
            ac_arg src_args[] = {args->vertex_id, args->tcs_rel_ids, args->tcs_patch_id};
            ac_arg dst_args[] = {args->instance_id, args->vs_rel_patch_id, args->vertex_id};
            for (unsigned i = 0; i < 3; i++) {
               bld.vop2(aco_opcode::v_cndmask_b32, Definition(get_arg_reg(args, dst_args[i]), v1),
                        get_arg_fixed(args, src_args[i]), get_arg_fixed(args, dst_args[i]),
                        Operand(vcc, bld.lm));
            }
         }

         if (needs_vertex_index)
            bld.vadd32(Definition(vertex_index, v1), get_arg_fixed(args, args->base_vertex),
                       get_arg_fixed(args, args->vertex_id), false, Operand(s2), true);
         if (needs_instance_index)
            bld.vadd32(Definition(instance_index, v1), start_instance, instance_id, false,
                       Operand(s2), true);
         if (needs_start_instance)
            bld.vop1(aco_opcode::v_mov_b32, Definition(start_instance_vgpr, v1), start_instance);
      }

      wait_for_smem_loads(bld);

      for (unsigned i = 0; i < num_descs;) {
         PhysReg dest(attributes_start.reg() + loc * 4u);

         /* calculate index */
         Operand fetch_index = Operand(vertex_index, v1);
         if (pinfo->instance_rate_inputs & (1u << loc)) {
            if (!(pinfo->zero_divisors & (1u << loc))) {
               fetch_index = instance_id;
               if (pinfo->nontrivial_divisors & (1u << loc)) {
                  unsigned index = util_bitcount(pinfo->nontrivial_divisors & BITFIELD_MASK(loc));
                  fetch_index = calc_nontrivial_instance_id(
                     bld, args, pinfo, index, instance_id, start_instance, prolog_input,
                     nontrivial_tmp_vgpr0, nontrivial_tmp_vgpr1);
               } else {
                  fetch_index = Operand(instance_index, v1);
               }
            } else {
               fetch_index = Operand(start_instance_vgpr, v1);
            }
         }

         /* perform load */
         PhysReg cur_desc = desc.advance(i * 16);
         if ((pinfo->misaligned_mask & (1u << loc))) {
            const struct ac_vtx_format_info* vtx_info = &vtx_info_table[pinfo->formats[loc]];

            assert(vtx_info->has_hw_format & 0x1);
            unsigned dfmt = vtx_info->hw_format[0] & 0xf;
            unsigned nfmt = vtx_info->hw_format[0] >> 4;

            for (unsigned j = 0; j < (vtx_info->chan_byte_size ? vtx_info->num_channels : 1); j++) {
               bool post_shuffle = pinfo->post_shuffle & (1u << loc);
               unsigned offset = vtx_info->chan_byte_size * (post_shuffle && j < 3 ? 2 - j : j);
               unsigned soffset = 0, const_offset = 0;

               /* We need to use soffset on GFX6-7 to avoid being considered
                * out-of-bounds when offset>=stride. GFX12 doesn't support a
                * non-zero constant soffset.
                */
               if (program->gfx_level >= GFX12) {
                  const_offset = offset;
               } else {
                  soffset = offset;
               }

               if ((pinfo->unaligned_mask & (1u << loc)) && vtx_info->chan_byte_size <= 4)
                  load_unaligned_vs_attrib(bld, dest.advance(j * 4u), Operand(cur_desc, s4),
                                           fetch_index, offset, vtx_info, &unaligned_state);
               else if (vtx_info->chan_byte_size == 8)
                  bld.mtbuf(aco_opcode::tbuffer_load_format_xy,
                            Definition(dest.advance(j * 8u), v2), Operand(cur_desc, s4),
                            fetch_index, Operand::c32(soffset), dfmt, nfmt, const_offset, false,
                            true);
               else
                  bld.mtbuf(aco_opcode::tbuffer_load_format_x, Definition(dest.advance(j * 4u), v1),
                            Operand(cur_desc, s4), fetch_index, Operand::c32(soffset), dfmt, nfmt,
                            const_offset, false, true);
            }

            unsigned slots = vtx_info->chan_byte_size == 8 && vtx_info->num_channels > 2 ? 2 : 1;
            loc += slots;
            i += slots;
         } else {
            bld.mubuf(aco_opcode::buffer_load_format_xyzw, Definition(dest, v4),
                      Operand(cur_desc, s4), fetch_index, Operand::c32(0u), 0u, false, true);
            loc++;
            i++;
         }
      }
   }

   uint32_t constant_mask = pinfo->misaligned_mask;
   while (constant_mask) {
      unsigned loc = u_bit_scan(&constant_mask);
      const struct ac_vtx_format_info* vtx_info = &vtx_info_table[pinfo->formats[loc]];

      /* 22.1.1. Attribute Location and Component Assignment of Vulkan 1.3 specification:
       * For 64-bit data types, no default attribute values are provided. Input variables must
       * not use more components than provided by the attribute.
       */
      if (vtx_info->chan_byte_size == 8) {
         if (vtx_info->num_channels > 2)
            u_bit_scan(&constant_mask);
         continue;
      }

      assert(vtx_info->has_hw_format & 0x1);
      unsigned nfmt = vtx_info->hw_format[0] >> 4;

      uint32_t one = nfmt == V_008F0C_BUF_NUM_FORMAT_UINT || nfmt == V_008F0C_BUF_NUM_FORMAT_SINT
                        ? 1u
                        : 0x3f800000u;
      PhysReg dest(attributes_start.reg() + loc * 4u);
      for (unsigned j = vtx_info->num_channels; j < 4; j++) {
         bld.vop1(aco_opcode::v_mov_b32, Definition(dest.advance(j * 4u), v1),
                  Operand::c32(j == 3 ? one : 0u));
      }
   }

   convert_current_unaligned_vs_attribs(bld, &unaligned_state);

   if (pinfo->alpha_adjust_lo | pinfo->alpha_adjust_hi)
      wait_for_vmem_loads(bld);

   /* For 2_10_10_10 formats the alpha is handled as unsigned by pre-vega HW.
    * so we may need to fix it up. */
   u_foreach_bit (loc, (pinfo->alpha_adjust_lo | pinfo->alpha_adjust_hi)) {
      PhysReg alpha(attributes_start.reg() + loc * 4u + 3);

      unsigned alpha_adjust = (pinfo->alpha_adjust_lo >> loc) & 0x1;
      alpha_adjust |= ((pinfo->alpha_adjust_hi >> loc) & 0x1) << 1;

      if (alpha_adjust == AC_ALPHA_ADJUST_SSCALED)
         bld.vop1(aco_opcode::v_cvt_u32_f32, Definition(alpha, v1), Operand(alpha, v1));

      /* For the integer-like cases, do a natural sign extension.
       *
       * For the SNORM case, the values are 0.0, 0.333, 0.666, 1.0
       * and happen to contain 0, 1, 2, 3 as the two LSBs of the
       * exponent.
       */
      unsigned offset = alpha_adjust == AC_ALPHA_ADJUST_SNORM ? 23u : 0u;
      bld.vop3(aco_opcode::v_bfe_i32, Definition(alpha, v1), Operand(alpha, v1),
               Operand::c32(offset), Operand::c32(2u));

      /* Convert back to the right type. */
      if (alpha_adjust == AC_ALPHA_ADJUST_SNORM) {
         bld.vop1(aco_opcode::v_cvt_f32_i32, Definition(alpha, v1), Operand(alpha, v1));
         bld.vop2(aco_opcode::v_max_f32, Definition(alpha, v1), Operand::c32(0xbf800000u),
                  Operand(alpha, v1));
      } else if (alpha_adjust == AC_ALPHA_ADJUST_SSCALED) {
         bld.vop1(aco_opcode::v_cvt_f32_i32, Definition(alpha, v1), Operand(alpha, v1));
      }
   }

   block->kind |= block_kind_uniform;

   /* continue on to the main shader */
   Operand continue_pc = get_arg_fixed(args, pinfo->inputs);
   if (has_nontrivial_divisors) {
      bld.smem(aco_opcode::s_load_dwordx2, Definition(prolog_input, s2),
               get_arg_fixed(args, pinfo->inputs), Operand::c32(0u));
      wait_for_smem_loads(bld);
      continue_pc = Operand(prolog_input, s2);
   }

   /* Wait for all pending VMEM loads when the prolog loads large 64-bit
    * attributes because the vertex shader isn't required to consume all of
    * them and they might be overwritten. This isn't the most optimal solution
    * but 64-bit vertex attributes are rarely used.
    */
   if (is_last_attr_large)
      wait_for_vmem_loads(bld);

   bld.sop1(aco_opcode::s_setpc_b64, continue_pc);

   program->config->float_mode = program->blocks[0].fp_mode.val;
   program->config->num_vgprs = std::min<uint16_t>(get_vgpr_alloc(program, num_vgprs), 256);
   program->config->num_sgprs = get_sgpr_alloc(program, num_sgprs);
}

} // namespace aco
