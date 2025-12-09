/*
 * Copyright © 2023 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "aco_builder.h"
#include "aco_instruction_selection.h"
#include "aco_interface.h"
#include "aco_ir.h"

namespace aco {

void
select_rt_prolog(Program* program, ac_shader_config* config,
                 const struct aco_compiler_options* options, const struct aco_shader_info* info,
                 const struct ac_shader_args* in_args, const struct ac_shader_args* out_args)
{
   init_program(program, compute_cs, info, options->gfx_level, options->family, options->wgp_mode,
                config);
   Block* block = program->create_and_insert_block();
   block->kind = block_kind_top_level;
   program->workgroup_size = info->workgroup_size;
   program->wave_size = info->workgroup_size;
   calc_min_waves(program);
   Builder bld(program, block);
   block->instructions.reserve(32);
   unsigned num_sgprs = MAX2(in_args->num_sgprs_used, out_args->num_sgprs_used);
   unsigned num_vgprs = MAX2(in_args->num_vgprs_used, out_args->num_vgprs_used);

   /* Inputs:
    * Ring offsets:                s[0-1]
    * Indirect descriptor sets:    s[2]
    * Push constants pointer:      s[3]
    * Dynamic descriptors:         s[4]
    * Traversal shader address:    s[5]
    * SBT descriptors:             s[6-7]
    * Ray launch size address:     s[8-9]
    * Dynamic callable stack base: s[10]
    * Workgroup IDs (xyz):         s[11], s[12], s[13]
    * Scratch offset:              s[14]
    * Local invocation IDs:        v[0-2]
    */
   PhysReg in_ring_offsets = get_arg_reg(in_args, in_args->ring_offsets);
   PhysReg in_sbt_desc = get_arg_reg(in_args, in_args->rt.sbt_descriptors);
   PhysReg in_launch_size_addr = get_arg_reg(in_args, in_args->rt.launch_size_addr);
   PhysReg in_stack_base = get_arg_reg(in_args, in_args->rt.dynamic_callable_stack_base);
   PhysReg in_wg_id_x;
   PhysReg in_wg_id_y;
   PhysReg in_wg_id_z;
   PhysReg in_scratch_offset;
   if (options->gfx_level < GFX12) {
      in_wg_id_x = get_arg_reg(in_args, in_args->workgroup_ids[0]);
      in_wg_id_y = get_arg_reg(in_args, in_args->workgroup_ids[1]);
      in_wg_id_z = get_arg_reg(in_args, in_args->workgroup_ids[2]);
   } else {
      in_wg_id_x = PhysReg(108 + 9 /*ttmp9*/);
      in_wg_id_y = PhysReg(108 + 7 /*ttmp7*/);
   }
   if (options->gfx_level < GFX11)
      in_scratch_offset = get_arg_reg(in_args, in_args->scratch_offset);
   struct ac_arg arg_id = options->gfx_level >= GFX11 ? in_args->local_invocation_ids_packed
                                                      : in_args->local_invocation_id_x;
   PhysReg in_local_ids[2] = {
      get_arg_reg(in_args, arg_id),
      get_arg_reg(in_args, arg_id).advance(4),
   };

   /* Outputs:
    * Callee shader PC:            s[0-1]
    * Indirect descriptor sets:    s[2]
    * Push constants pointer:      s[3]
    * Dynamic descriptors:         s[4]
    * Traversal shader address:    s[5]
    * SBT descriptors:             s[6-7]
    * Ray launch sizes (xyz):      s[8], s[9], s[10]
    * Scratch offset (<GFX9 only): s[11]
    * Ring offsets (<GFX9 only):   s[12-13]
    * Ray launch IDs:              v[0-2]
    * Stack pointer:               v[3]
    * Shader VA:                   v[4-5]
    * Shader Record Ptr:           v[6-7]
    */
   PhysReg out_uniform_shader_addr = get_arg_reg(out_args, out_args->rt.uniform_shader_addr);
   PhysReg out_launch_size_x = get_arg_reg(out_args, out_args->rt.launch_sizes[0]);
   PhysReg out_launch_size_y = get_arg_reg(out_args, out_args->rt.launch_sizes[1]);
   PhysReg out_launch_size_z = get_arg_reg(out_args, out_args->rt.launch_sizes[2]);
   PhysReg out_launch_ids[3];
   for (unsigned i = 0; i < 3; i++)
      out_launch_ids[i] = get_arg_reg(out_args, out_args->rt.launch_ids[i]);
   PhysReg out_stack_ptr = get_arg_reg(out_args, out_args->rt.dynamic_callable_stack_base);
   PhysReg out_record_ptr = get_arg_reg(out_args, out_args->rt.shader_record);

   /* Temporaries: */
   num_sgprs = align(num_sgprs, 2);
   PhysReg tmp_raygen_sbt = PhysReg{num_sgprs};
   num_sgprs += 2;
   PhysReg tmp_ring_offsets = PhysReg{num_sgprs};
   num_sgprs += 2;
   PhysReg tmp_wg_id_x_times_size = PhysReg{num_sgprs};
   num_sgprs++;

   PhysReg tmp_invocation_idx = PhysReg{256 + num_vgprs++};

   /* Confirm some assumptions about register aliasing */
   assert(in_ring_offsets == out_uniform_shader_addr);
   assert(get_arg_reg(in_args, in_args->push_constants) ==
          get_arg_reg(out_args, out_args->push_constants));
   assert(get_arg_reg(in_args, in_args->dynamic_descriptors) ==
          get_arg_reg(out_args, out_args->dynamic_descriptors));
   assert(get_arg_reg(in_args, in_args->rt.sbt_descriptors) ==
          get_arg_reg(out_args, out_args->rt.sbt_descriptors));
   assert(get_arg_reg(in_args, in_args->rt.traversal_shader_addr) ==
          get_arg_reg(out_args, out_args->rt.traversal_shader_addr));
   assert(in_launch_size_addr == out_launch_size_x);
   assert(in_stack_base == out_launch_size_z);
   assert(in_local_ids[0] == out_launch_ids[0]);

   /* <gfx9 reads in_scratch_offset at the end of the prolog to write out the scratch_offset
    * arg. Make sure no other outputs have overwritten it by then.
    */
   assert(options->gfx_level >= GFX9 || in_scratch_offset.reg() >= out_args->num_sgprs_used);

   /* load raygen sbt */
   bld.smem(aco_opcode::s_load_dwordx2, Definition(tmp_raygen_sbt, s2), Operand(in_sbt_desc, s2),
            Operand::c32(0u));

   /* init scratch */
   if (options->gfx_level < GFX9) {
      /* copy ring offsets to temporary location*/
      bld.sop1(aco_opcode::s_mov_b64, Definition(tmp_ring_offsets, s2),
               Operand(in_ring_offsets, s2));
   } else if (options->gfx_level < GFX11) {
      hw_init_scratch(bld, Definition(in_ring_offsets, s1), Operand(in_ring_offsets, s2),
                      Operand(in_scratch_offset, s1));
   }

   /* set stack ptr */
   bld.vop1(aco_opcode::v_mov_b32, Definition(out_stack_ptr, v1), Operand(in_stack_base, s1));

   /* load raygen address */
   bld.smem(aco_opcode::s_load_dwordx2, Definition(out_uniform_shader_addr, s2),
            Operand(tmp_raygen_sbt, s2), Operand::c32(0u));

   /* load ray launch sizes */
   assert(out_launch_size_x.reg() % 4 == 0);
   if (options->gfx_level >= GFX12) {
      bld.smem(aco_opcode::s_load_dwordx3, Definition(out_launch_size_x, s3),
               Operand(in_launch_size_addr, s2), Operand::c32(0u));
   } else {
      bld.smem(aco_opcode::s_load_dword, Definition(out_launch_size_z, s1),
               Operand(in_launch_size_addr, s2), Operand::c32(8u));
      bld.smem(aco_opcode::s_load_dwordx2, Definition(out_launch_size_x, s2),
               Operand(in_launch_size_addr, s2), Operand::c32(0u));
   }

   /* calculate ray launch ids */
   if (options->gfx_level >= GFX11) {
      /* Thread IDs are packed in VGPR0, 10 bits per component. */
      bld.vop3(aco_opcode::v_bfe_u32, Definition(in_local_ids[1], v1), Operand(in_local_ids[0], v1),
               Operand::c32(10u), Operand::c32(3u));
      bld.vop2(aco_opcode::v_and_b32, Definition(in_local_ids[0], v1), Operand::c32(0x7),
               Operand(in_local_ids[0], v1));
   }
   /* Do this backwards to reduce some RAW hazards on GFX11+ */
   if (options->gfx_level >= GFX12) {
      bld.vop2_e64(aco_opcode::v_lshrrev_b32, Definition(out_launch_ids[2], v1), Operand::c32(16),
                   Operand(in_wg_id_y, s1));
      bld.vop3(aco_opcode::v_mad_u32_u16, Definition(out_launch_ids[1], v1),
               Operand(in_wg_id_y, s1), Operand::c32(program->workgroup_size == 32 ? 4 : 8),
               Operand(in_local_ids[1], v1));
   } else {
      bld.vop1(aco_opcode::v_mov_b32, Definition(out_launch_ids[2], v1), Operand(in_wg_id_z, s1));
      bld.vop3(aco_opcode::v_mad_u32_u24, Definition(out_launch_ids[1], v1),
               Operand(in_wg_id_y, s1), Operand::c32(program->workgroup_size == 32 ? 4 : 8),
               Operand(in_local_ids[1], v1));
   }
   bld.vop3(aco_opcode::v_mad_u32_u24, Definition(out_launch_ids[0], v1), Operand(in_wg_id_x, s1),
            Operand::c32(8), Operand(in_local_ids[0], v1));

   /* calculate shader record ptr: SBT + RADV_RT_HANDLE_SIZE */
   if (options->gfx_level < GFX9) {
      bld.vop2_e64(aco_opcode::v_add_co_u32, Definition(out_record_ptr, v1), Definition(vcc, s2),
                   Operand(tmp_raygen_sbt, s1), Operand::c32(32u));
   } else {
      bld.vop2_e64(aco_opcode::v_add_u32, Definition(out_record_ptr, v1),
                   Operand(tmp_raygen_sbt, s1), Operand::c32(32u));
   }
   bld.vop1(aco_opcode::v_mov_b32, Definition(out_record_ptr.advance(4), v1),
            Operand(tmp_raygen_sbt.advance(4), s1));

   /* For 1D dispatches converted into 2D ones, we need to fix up the launch IDs.
    * Calculating the 1D launch ID is: id = local_invocation_index + (wg_id.x * wg_size).
    * tmp_wg_id_x_times_size now holds wg_id.x * wg_size.
    */
   bld.sop2(aco_opcode::s_lshl_b32, Definition(tmp_wg_id_x_times_size, s1), Definition(scc, s1),
            Operand(in_wg_id_x, s1), Operand::c32(program->workgroup_size == 32 ? 5 : 6));

   /* Calculate and add local_invocation_index */
   bld.vop3(aco_opcode::v_mbcnt_lo_u32_b32, Definition(tmp_invocation_idx, v1), Operand::c32(-1u),
            Operand(tmp_wg_id_x_times_size, s1));
   if (program->wave_size == 64) {
      if (program->gfx_level <= GFX7)
         bld.vop2(aco_opcode::v_mbcnt_hi_u32_b32, Definition(tmp_invocation_idx, v1),
                  Operand::c32(-1u), Operand(tmp_invocation_idx, v1));
      else
         bld.vop3(aco_opcode::v_mbcnt_hi_u32_b32_e64, Definition(tmp_invocation_idx, v1),
                  Operand::c32(-1u), Operand(tmp_invocation_idx, v1));
   }

   /* Make fixup operations a no-op if this is not a converted 2D dispatch. */
   bld.sopc(aco_opcode::s_cmp_lg_u32, Definition(scc, s1),
            Operand::c32(ACO_RT_CONVERTED_2D_LAUNCH_SIZE), Operand(out_launch_size_y, s1));
   bld.sop2(Builder::s_cselect, Definition(vcc, bld.lm),
            Operand::c32_or_c64(-1u, program->wave_size == 64),
            Operand::c32_or_c64(0, program->wave_size == 64), Operand(scc, s1));
   bld.sop2(aco_opcode::s_cselect_b32, Definition(out_launch_size_y, s1),
            Operand(out_launch_size_y, s1), Operand::c32(1), Operand(scc, s1));
   bld.vop2(aco_opcode::v_cndmask_b32, Definition(out_launch_ids[0], v1),
            Operand(tmp_invocation_idx, v1), Operand(out_launch_ids[0], v1), Operand(vcc, bld.lm));
   bld.vop2(aco_opcode::v_cndmask_b32, Definition(out_launch_ids[1], v1), Operand::zero(),
            Operand(out_launch_ids[1], v1), Operand(vcc, bld.lm));

   if (options->gfx_level < GFX9) {
      /* write scratch/ring offsets to outputs, if needed */
      bld.sop1(aco_opcode::s_mov_b32,
               Definition(get_arg_reg(out_args, out_args->scratch_offset), s1),
               Operand(in_scratch_offset, s1));
      bld.sop1(aco_opcode::s_mov_b64, Definition(get_arg_reg(out_args, out_args->ring_offsets), s2),
               Operand(tmp_ring_offsets, s2));
   }

   /* jump to raygen */
   bld.sop1(aco_opcode::s_setpc_b64, Operand(out_uniform_shader_addr, s2));

   program->config->float_mode = program->blocks[0].fp_mode.val;
   program->config->num_vgprs = get_vgpr_alloc(program, num_vgprs);
   program->config->num_sgprs = get_sgpr_alloc(program, num_sgprs);
}

} // namespace aco
