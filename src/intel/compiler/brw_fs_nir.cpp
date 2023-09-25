/*
 * Copyright © 2010 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "compiler/glsl/ir.h"
#include "brw_fs.h"
#include "brw_nir.h"
#include "brw_rt.h"
#include "brw_eu.h"
#include "nir_search_helpers.h"
#include "util/u_math.h"
#include "util/bitscan.h"

#include <vector>

using namespace brw;

void
fs_visitor::emit_nir_code()
{
   emit_shader_float_controls_execution_mode();

   /* emit the arrays used for inputs and outputs - load/store intrinsics will
    * be converted to reads/writes of these arrays
    */
   nir_setup_outputs();
   nir_setup_uniforms();
   nir_emit_system_values();
   last_scratch = ALIGN(nir->scratch_size, 4) * dispatch_width;

   nir_emit_impl(nir_shader_get_entrypoint((nir_shader *)nir));

   bld.emit(SHADER_OPCODE_HALT_TARGET);
}

void
fs_visitor::nir_setup_outputs()
{
   if (stage == MESA_SHADER_TESS_CTRL ||
       stage == MESA_SHADER_TASK ||
       stage == MESA_SHADER_MESH ||
       stage == MESA_SHADER_FRAGMENT)
      return;

   unsigned vec4s[VARYING_SLOT_TESS_MAX] = { 0, };

   /* Calculate the size of output registers in a separate pass, before
    * allocating them.  With ARB_enhanced_layouts, multiple output variables
    * may occupy the same slot, but have different type sizes.
    */
   nir_foreach_shader_out_variable(var, nir) {
      const int loc = var->data.driver_location;
      const unsigned var_vec4s =
         var->data.compact ? DIV_ROUND_UP(var->data.location_frac + glsl_get_length(var->type), 4)
                           : type_size_vec4(var->type, true);
      vec4s[loc] = MAX2(vec4s[loc], var_vec4s);
   }

   for (unsigned loc = 0; loc < ARRAY_SIZE(vec4s);) {
      if (vec4s[loc] == 0) {
         loc++;
         continue;
      }

      unsigned reg_size = vec4s[loc];

      /* Check if there are any ranges that start within this range and extend
       * past it. If so, include them in this allocation.
       */
      for (unsigned i = 1; i < reg_size; i++) {
         assert(i + loc < ARRAY_SIZE(vec4s));
         reg_size = MAX2(vec4s[i + loc] + i, reg_size);
      }

      fs_reg reg = bld.vgrf(BRW_REGISTER_TYPE_F, 4 * reg_size);
      for (unsigned i = 0; i < reg_size; i++) {
         assert(loc + i < ARRAY_SIZE(outputs));
         outputs[loc + i] = offset(reg, bld, 4 * i);
      }

      loc += reg_size;
   }
}

void
fs_visitor::nir_setup_uniforms()
{
   /* Only the first compile gets to set up uniforms. */
   if (push_constant_loc)
      return;

   uniforms = nir->num_uniforms / 4;

   if (gl_shader_stage_is_compute(stage) && devinfo->verx10 < 125) {
      /* Add uniforms for builtins after regular NIR uniforms. */
      assert(uniforms == prog_data->nr_params);

      /* Subgroup ID must be the last uniform on the list.  This will make
       * easier later to split between cross thread and per thread
       * uniforms.
       */
      uint32_t *param = brw_stage_prog_data_add_params(prog_data, 1);
      *param = BRW_PARAM_BUILTIN_SUBGROUP_ID;
      uniforms++;
   }
}

static bool
emit_system_values_block(nir_block *block, fs_visitor *v)
{
   fs_reg *reg;

   nir_foreach_instr(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      switch (intrin->intrinsic) {
      case nir_intrinsic_load_vertex_id:
      case nir_intrinsic_load_base_vertex:
         unreachable("should be lowered by nir_lower_system_values().");

      case nir_intrinsic_load_vertex_id_zero_base:
      case nir_intrinsic_load_is_indexed_draw:
      case nir_intrinsic_load_first_vertex:
      case nir_intrinsic_load_instance_id:
      case nir_intrinsic_load_base_instance:
         unreachable("should be lowered by brw_nir_lower_vs_inputs().");
         break;

      case nir_intrinsic_load_draw_id:
         /* For Task/Mesh, draw_id will be handled later in
          * nir_emit_mesh_task_intrinsic().
          */
         if (!gl_shader_stage_is_mesh(v->stage))
            unreachable("should be lowered by brw_nir_lower_vs_inputs().");
         break;

      case nir_intrinsic_load_invocation_id:
         if (v->stage == MESA_SHADER_TESS_CTRL)
            break;
         assert(v->stage == MESA_SHADER_GEOMETRY);
         reg = &v->nir_system_values[SYSTEM_VALUE_INVOCATION_ID];
         if (reg->file == BAD_FILE) {
            const fs_builder abld = v->bld.annotate("gl_InvocationID", NULL);
            fs_reg g1(retype(brw_vec8_grf(1, 0), BRW_REGISTER_TYPE_UD));
            fs_reg iid = abld.vgrf(BRW_REGISTER_TYPE_UD, 1);
            abld.SHR(iid, g1, brw_imm_ud(27u));
            *reg = iid;
         }
         break;

      case nir_intrinsic_load_sample_pos:
      case nir_intrinsic_load_sample_pos_or_center:
         assert(v->stage == MESA_SHADER_FRAGMENT);
         reg = &v->nir_system_values[SYSTEM_VALUE_SAMPLE_POS];
         if (reg->file == BAD_FILE)
            *reg = v->emit_samplepos_setup();
         break;

      case nir_intrinsic_load_sample_id:
         assert(v->stage == MESA_SHADER_FRAGMENT);
         reg = &v->nir_system_values[SYSTEM_VALUE_SAMPLE_ID];
         if (reg->file == BAD_FILE)
            *reg = v->emit_sampleid_setup();
         break;

      case nir_intrinsic_load_sample_mask_in:
         assert(v->stage == MESA_SHADER_FRAGMENT);
         assert(v->devinfo->ver >= 7);
         reg = &v->nir_system_values[SYSTEM_VALUE_SAMPLE_MASK_IN];
         if (reg->file == BAD_FILE)
            *reg = v->emit_samplemaskin_setup();
         break;

      case nir_intrinsic_load_workgroup_id:
      case nir_intrinsic_load_workgroup_id_zero_base:
         assert(gl_shader_stage_uses_workgroup(v->stage));
         reg = &v->nir_system_values[SYSTEM_VALUE_WORKGROUP_ID];
         if (reg->file == BAD_FILE)
            *reg = v->emit_work_group_id_setup();
         break;

      case nir_intrinsic_load_helper_invocation:
         assert(v->stage == MESA_SHADER_FRAGMENT);
         reg = &v->nir_system_values[SYSTEM_VALUE_HELPER_INVOCATION];
         if (reg->file == BAD_FILE) {
            const fs_builder abld =
               v->bld.annotate("gl_HelperInvocation", NULL);

            /* On Gfx6+ (gl_HelperInvocation is only exposed on Gfx7+) the
             * pixel mask is in g1.7 of the thread payload.
             *
             * We move the per-channel pixel enable bit to the low bit of each
             * channel by shifting the byte containing the pixel mask by the
             * vector immediate 0x76543210UV.
             *
             * The region of <1,8,0> reads only 1 byte (the pixel masks for
             * subspans 0 and 1) in SIMD8 and an additional byte (the pixel
             * masks for 2 and 3) in SIMD16.
             */
            fs_reg shifted = abld.vgrf(BRW_REGISTER_TYPE_UW, 1);

            for (unsigned i = 0; i < DIV_ROUND_UP(v->dispatch_width, 16); i++) {
               const fs_builder hbld = abld.group(MIN2(16, v->dispatch_width), i);
               hbld.SHR(offset(shifted, hbld, i),
                        stride(retype(brw_vec1_grf(1 + i, 7),
                                      BRW_REGISTER_TYPE_UB),
                               1, 8, 0),
                        brw_imm_v(0x76543210));
            }

            /* A set bit in the pixel mask means the channel is enabled, but
             * that is the opposite of gl_HelperInvocation so we need to invert
             * the mask.
             *
             * The negate source-modifier bit of logical instructions on Gfx8+
             * performs 1's complement negation, so we can use that instead of
             * a NOT instruction.
             */
            fs_reg inverted = negate(shifted);
            if (v->devinfo->ver < 8) {
               inverted = abld.vgrf(BRW_REGISTER_TYPE_UW);
               abld.NOT(inverted, shifted);
            }

            /* We then resolve the 0/1 result to 0/~0 boolean values by ANDing
             * with 1 and negating.
             */
            fs_reg anded = abld.vgrf(BRW_REGISTER_TYPE_UD, 1);
            abld.AND(anded, inverted, brw_imm_uw(1));

            fs_reg dst = abld.vgrf(BRW_REGISTER_TYPE_D, 1);
            abld.MOV(dst, negate(retype(anded, BRW_REGISTER_TYPE_D)));
            *reg = dst;
         }
         break;

      case nir_intrinsic_load_frag_shading_rate:
         reg = &v->nir_system_values[SYSTEM_VALUE_FRAG_SHADING_RATE];
         if (reg->file == BAD_FILE)
            *reg = v->emit_shading_rate_setup();
         break;

      default:
         break;
      }
   }

   return true;
}

void
fs_visitor::nir_emit_system_values()
{
   nir_system_values = ralloc_array(mem_ctx, fs_reg, SYSTEM_VALUE_MAX);
   for (unsigned i = 0; i < SYSTEM_VALUE_MAX; i++) {
      nir_system_values[i] = fs_reg();
   }

   /* Always emit SUBGROUP_INVOCATION.  Dead code will clean it up if we
    * never end up using it.
    */
   {
      const fs_builder abld = bld.annotate("gl_SubgroupInvocation", NULL);
      fs_reg &reg = nir_system_values[SYSTEM_VALUE_SUBGROUP_INVOCATION];
      reg = abld.vgrf(BRW_REGISTER_TYPE_UW);
      abld.UNDEF(reg);

      const fs_builder allbld8 = abld.group(8, 0).exec_all();
      allbld8.MOV(reg, brw_imm_v(0x76543210));
      if (dispatch_width > 8)
         allbld8.ADD(byte_offset(reg, 16), reg, brw_imm_uw(8u));
      if (dispatch_width > 16) {
         const fs_builder allbld16 = abld.group(16, 0).exec_all();
         allbld16.ADD(byte_offset(reg, 32), reg, brw_imm_uw(16u));
      }
   }

   nir_function_impl *impl = nir_shader_get_entrypoint((nir_shader *)nir);
   nir_foreach_block(block, impl)
      emit_system_values_block(block, this);
}

void
fs_visitor::nir_emit_impl(nir_function_impl *impl)
{
   nir_locals = ralloc_array(mem_ctx, fs_reg, impl->reg_alloc);
   for (unsigned i = 0; i < impl->reg_alloc; i++) {
      nir_locals[i] = fs_reg();
   }

   foreach_list_typed(nir_register, reg, node, &impl->registers) {
      unsigned array_elems =
         reg->num_array_elems == 0 ? 1 : reg->num_array_elems;
      unsigned size = array_elems * reg->num_components;
      const brw_reg_type reg_type = reg->bit_size == 8 ? BRW_REGISTER_TYPE_B :
         brw_reg_type_from_bit_size(reg->bit_size, BRW_REGISTER_TYPE_F);
      nir_locals[reg->index] = bld.vgrf(reg_type, size);
   }

   nir_ssa_values = reralloc(mem_ctx, nir_ssa_values, fs_reg,
                             impl->ssa_alloc);

   nir_resource_insts = reralloc(mem_ctx, nir_resource_insts, fs_inst *,
                                 impl->ssa_alloc);
   memset(nir_resource_insts, 0, sizeof(nir_resource_insts[0]) * impl->ssa_alloc);

   nir_ssa_bind_infos = reralloc(mem_ctx, nir_ssa_bind_infos,
                                 struct brw_fs_bind_info,
                                 impl->ssa_alloc);
   memset(nir_ssa_bind_infos, 0,
          sizeof(nir_ssa_bind_infos[0]) * impl->ssa_alloc);

   nir_resource_values = reralloc(mem_ctx, nir_resource_values, fs_reg,
                                  impl->ssa_alloc);

   nir_emit_cf_list(&impl->body);
}

void
fs_visitor::nir_emit_cf_list(exec_list *list)
{
   exec_list_validate(list);
   foreach_list_typed(nir_cf_node, node, node, list) {
      switch (node->type) {
      case nir_cf_node_if:
         nir_emit_if(nir_cf_node_as_if(node));
         break;

      case nir_cf_node_loop:
         nir_emit_loop(nir_cf_node_as_loop(node));
         break;

      case nir_cf_node_block:
         nir_emit_block(nir_cf_node_as_block(node));
         break;

      default:
         unreachable("Invalid CFG node block");
      }
   }
}

void
fs_visitor::nir_emit_if(nir_if *if_stmt)
{
   bool invert;
   fs_reg cond_reg;

   /* If the condition has the form !other_condition, use other_condition as
    * the source, but invert the predicate on the if instruction.
    */
   nir_alu_instr *cond = nir_src_as_alu_instr(if_stmt->condition);
   if (cond != NULL && cond->op == nir_op_inot) {
      invert = true;
      cond_reg = get_nir_src(cond->src[0].src);
      cond_reg = offset(cond_reg, bld, cond->src[0].swizzle[0]);
   } else {
      invert = false;
      cond_reg = get_nir_src(if_stmt->condition);
   }

   /* first, put the condition into f0 */
   fs_inst *inst = bld.MOV(bld.null_reg_d(),
                           retype(cond_reg, BRW_REGISTER_TYPE_D));
   inst->conditional_mod = BRW_CONDITIONAL_NZ;

   bld.IF(BRW_PREDICATE_NORMAL)->predicate_inverse = invert;

   nir_emit_cf_list(&if_stmt->then_list);

   if (!nir_cf_list_is_empty_block(&if_stmt->else_list)) {
      bld.emit(BRW_OPCODE_ELSE);
      nir_emit_cf_list(&if_stmt->else_list);
   }

   bld.emit(BRW_OPCODE_ENDIF);

   if (devinfo->ver < 7)
      limit_dispatch_width(16, "Non-uniform control flow unsupported "
                           "in SIMD32 mode.");
}

void
fs_visitor::nir_emit_loop(nir_loop *loop)
{
   assert(!nir_loop_has_continue_construct(loop));
   bld.emit(BRW_OPCODE_DO);

   nir_emit_cf_list(&loop->body);

   bld.emit(BRW_OPCODE_WHILE);

   if (devinfo->ver < 7)
      limit_dispatch_width(16, "Non-uniform control flow unsupported "
                           "in SIMD32 mode.");
}

void
fs_visitor::nir_emit_block(nir_block *block)
{
   nir_foreach_instr(instr, block) {
      nir_emit_instr(instr);
   }
}

void
fs_visitor::nir_emit_instr(nir_instr *instr)
{
   const fs_builder abld = bld.annotate(NULL, instr);

   switch (instr->type) {
   case nir_instr_type_alu:
      nir_emit_alu(abld, nir_instr_as_alu(instr), true);
      break;

   case nir_instr_type_deref:
      unreachable("All derefs should've been lowered");
      break;

   case nir_instr_type_intrinsic:
      switch (stage) {
      case MESA_SHADER_VERTEX:
         nir_emit_vs_intrinsic(abld, nir_instr_as_intrinsic(instr));
         break;
      case MESA_SHADER_TESS_CTRL:
         nir_emit_tcs_intrinsic(abld, nir_instr_as_intrinsic(instr));
         break;
      case MESA_SHADER_TESS_EVAL:
         nir_emit_tes_intrinsic(abld, nir_instr_as_intrinsic(instr));
         break;
      case MESA_SHADER_GEOMETRY:
         nir_emit_gs_intrinsic(abld, nir_instr_as_intrinsic(instr));
         break;
      case MESA_SHADER_FRAGMENT:
         nir_emit_fs_intrinsic(abld, nir_instr_as_intrinsic(instr));
         break;
      case MESA_SHADER_COMPUTE:
      case MESA_SHADER_KERNEL:
         nir_emit_cs_intrinsic(abld, nir_instr_as_intrinsic(instr));
         break;
      case MESA_SHADER_RAYGEN:
      case MESA_SHADER_ANY_HIT:
      case MESA_SHADER_CLOSEST_HIT:
      case MESA_SHADER_MISS:
      case MESA_SHADER_INTERSECTION:
      case MESA_SHADER_CALLABLE:
         nir_emit_bs_intrinsic(abld, nir_instr_as_intrinsic(instr));
         break;
      case MESA_SHADER_TASK:
         nir_emit_task_intrinsic(abld, nir_instr_as_intrinsic(instr));
         break;
      case MESA_SHADER_MESH:
         nir_emit_mesh_intrinsic(abld, nir_instr_as_intrinsic(instr));
         break;
      default:
         unreachable("unsupported shader stage");
      }
      break;

   case nir_instr_type_tex:
      nir_emit_texture(abld, nir_instr_as_tex(instr));
      break;

   case nir_instr_type_load_const:
      nir_emit_load_const(abld, nir_instr_as_load_const(instr));
      break;

   case nir_instr_type_ssa_undef:
      /* We create a new VGRF for undefs on every use (by handling
       * them in get_nir_src()), rather than for each definition.
       * This helps register coalescing eliminate MOVs from undef.
       */
      break;

   case nir_instr_type_jump:
      nir_emit_jump(abld, nir_instr_as_jump(instr));
      break;

   default:
      unreachable("unknown instruction type");
   }
}

/**
 * Recognizes a parent instruction of nir_op_extract_* and changes the type to
 * match instr.
 */
bool
fs_visitor::optimize_extract_to_float(nir_alu_instr *instr,
                                      const fs_reg &result)
{
   if (!instr->src[0].src.is_ssa ||
       !instr->src[0].src.ssa->parent_instr)
      return false;

   if (instr->src[0].src.ssa->parent_instr->type != nir_instr_type_alu)
      return false;

   nir_alu_instr *src0 =
      nir_instr_as_alu(instr->src[0].src.ssa->parent_instr);

   if (src0->op != nir_op_extract_u8 && src0->op != nir_op_extract_u16 &&
       src0->op != nir_op_extract_i8 && src0->op != nir_op_extract_i16)
      return false;

   unsigned element = nir_src_as_uint(src0->src[1].src);

   /* Element type to extract.*/
   const brw_reg_type type = brw_int_type(
      src0->op == nir_op_extract_u16 || src0->op == nir_op_extract_i16 ? 2 : 1,
      src0->op == nir_op_extract_i16 || src0->op == nir_op_extract_i8);

   fs_reg op0 = get_nir_src(src0->src[0].src);
   op0.type = brw_type_for_nir_type(devinfo,
      (nir_alu_type)(nir_op_infos[src0->op].input_types[0] |
                     nir_src_bit_size(src0->src[0].src)));
   op0 = offset(op0, bld, src0->src[0].swizzle[0]);

   bld.MOV(result, subscript(op0, type, element));
   return true;
}

bool
fs_visitor::optimize_frontfacing_ternary(nir_alu_instr *instr,
                                         const fs_reg &result)
{
   nir_intrinsic_instr *src0 = nir_src_as_intrinsic(instr->src[0].src);
   if (src0 == NULL || src0->intrinsic != nir_intrinsic_load_front_face)
      return false;

   if (!nir_src_is_const(instr->src[1].src) ||
       !nir_src_is_const(instr->src[2].src))
      return false;

   const float value1 = nir_src_as_float(instr->src[1].src);
   const float value2 = nir_src_as_float(instr->src[2].src);
   if (fabsf(value1) != 1.0f || fabsf(value2) != 1.0f)
      return false;

   /* nir_opt_algebraic should have gotten rid of bcsel(b, a, a) */
   assert(value1 == -value2);

   fs_reg tmp = vgrf(glsl_type::int_type);

   if (devinfo->ver >= 12) {
      /* Bit 15 of g1.1 is 0 if the polygon is front facing. */
      fs_reg g1 = fs_reg(retype(brw_vec1_grf(1, 1), BRW_REGISTER_TYPE_W));

      /* For (gl_FrontFacing ? 1.0 : -1.0), emit:
       *
       *    or(8)  tmp.1<2>W  g1.1<0,1,0>W  0x00003f80W
       *    and(8) dst<1>D    tmp<8,8,1>D   0xbf800000D
       *
       * and negate g1.1<0,1,0>W for (gl_FrontFacing ? -1.0 : 1.0).
       */
      if (value1 == -1.0f)
         g1.negate = true;

      bld.OR(subscript(tmp, BRW_REGISTER_TYPE_W, 1),
             g1, brw_imm_uw(0x3f80));
   } else if (devinfo->ver >= 6) {
      /* Bit 15 of g0.0 is 0 if the polygon is front facing. */
      fs_reg g0 = fs_reg(retype(brw_vec1_grf(0, 0), BRW_REGISTER_TYPE_W));

      /* For (gl_FrontFacing ? 1.0 : -1.0), emit:
       *
       *    or(8)  tmp.1<2>W  g0.0<0,1,0>W  0x00003f80W
       *    and(8) dst<1>D    tmp<8,8,1>D   0xbf800000D
       *
       * and negate g0.0<0,1,0>W for (gl_FrontFacing ? -1.0 : 1.0).
       *
       * This negation looks like it's safe in practice, because bits 0:4 will
       * surely be TRIANGLES
       */

      if (value1 == -1.0f) {
         g0.negate = true;
      }

      bld.OR(subscript(tmp, BRW_REGISTER_TYPE_W, 1),
             g0, brw_imm_uw(0x3f80));
   } else {
      /* Bit 31 of g1.6 is 0 if the polygon is front facing. */
      fs_reg g1_6 = fs_reg(retype(brw_vec1_grf(1, 6), BRW_REGISTER_TYPE_D));

      /* For (gl_FrontFacing ? 1.0 : -1.0), emit:
       *
       *    or(8)  tmp<1>D  g1.6<0,1,0>D  0x3f800000D
       *    and(8) dst<1>D  tmp<8,8,1>D   0xbf800000D
       *
       * and negate g1.6<0,1,0>D for (gl_FrontFacing ? -1.0 : 1.0).
       *
       * This negation looks like it's safe in practice, because bits 0:4 will
       * surely be TRIANGLES
       */

      if (value1 == -1.0f) {
         g1_6.negate = true;
      }

      bld.OR(tmp, g1_6, brw_imm_d(0x3f800000));
   }
   bld.AND(retype(result, BRW_REGISTER_TYPE_D), tmp, brw_imm_d(0xbf800000));

   return true;
}

static brw_rnd_mode
brw_rnd_mode_from_nir_op (const nir_op op) {
   switch (op) {
   case nir_op_f2f16_rtz:
      return BRW_RND_MODE_RTZ;
   case nir_op_f2f16_rtne:
      return BRW_RND_MODE_RTNE;
   default:
      unreachable("Operation doesn't support rounding mode");
   }
}

static brw_rnd_mode
brw_rnd_mode_from_execution_mode(unsigned execution_mode)
{
   if (nir_has_any_rounding_mode_rtne(execution_mode))
      return BRW_RND_MODE_RTNE;
   if (nir_has_any_rounding_mode_rtz(execution_mode))
      return BRW_RND_MODE_RTZ;
   return BRW_RND_MODE_UNSPECIFIED;
}

fs_reg
fs_visitor::prepare_alu_destination_and_sources(const fs_builder &bld,
                                                nir_alu_instr *instr,
                                                fs_reg *op,
                                                bool need_dest)
{
   fs_reg result =
      need_dest ? get_nir_dest(instr->dest.dest) : bld.null_reg_ud();

   result.type = brw_type_for_nir_type(devinfo,
      (nir_alu_type)(nir_op_infos[instr->op].output_type |
                     nir_dest_bit_size(instr->dest.dest)));

   assert(!instr->dest.saturate);

   for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
      /* We don't lower to source modifiers so they should not exist. */
      assert(!instr->src[i].abs);
      assert(!instr->src[i].negate);

      op[i] = get_nir_src(instr->src[i].src);
      op[i].type = brw_type_for_nir_type(devinfo,
         (nir_alu_type)(nir_op_infos[instr->op].input_types[i] |
                        nir_src_bit_size(instr->src[i].src)));
   }

   /* Move and vecN instrutions may still be vectored.  Return the raw,
    * vectored source and destination so that fs_visitor::nir_emit_alu can
    * handle it.  Other callers should not have to handle these kinds of
    * instructions.
    */
   switch (instr->op) {
   case nir_op_mov:
   case nir_op_vec2:
   case nir_op_vec3:
   case nir_op_vec4:
   case nir_op_vec8:
   case nir_op_vec16:
      return result;
   default:
      break;
   }

   /* At this point, we have dealt with any instruction that operates on
    * more than a single channel.  Therefore, we can just adjust the source
    * and destination registers for that channel and emit the instruction.
    */
   unsigned channel = 0;
   if (nir_op_infos[instr->op].output_size == 0) {
      /* Since NIR is doing the scalarizing for us, we should only ever see
       * vectorized operations with a single channel.
       */
      assert(util_bitcount(instr->dest.write_mask) == 1);
      channel = ffs(instr->dest.write_mask) - 1;

      result = offset(result, bld, channel);
   }

   for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
      assert(nir_op_infos[instr->op].input_sizes[i] < 2);
      op[i] = offset(op[i], bld, instr->src[i].swizzle[channel]);
   }

   return result;
}

void
fs_visitor::resolve_inot_sources(const fs_builder &bld, nir_alu_instr *instr,
                                 fs_reg *op)
{
   for (unsigned i = 0; i < 2; i++) {
      nir_alu_instr *inot_instr = nir_src_as_alu_instr(instr->src[i].src);

      if (inot_instr != NULL && inot_instr->op == nir_op_inot) {
         /* The source of the inot is now the source of instr. */
         prepare_alu_destination_and_sources(bld, inot_instr, &op[i], false);

         assert(!op[i].negate);
         op[i].negate = true;
      } else {
         op[i] = resolve_source_modifiers(bld, op[i]);
      }
   }
}

bool
fs_visitor::try_emit_b2fi_of_inot(const fs_builder &bld,
                                  fs_reg result,
                                  nir_alu_instr *instr)
{
   if (devinfo->ver < 6 || devinfo->verx10 >= 125)
      return false;

   nir_alu_instr *inot_instr = nir_src_as_alu_instr(instr->src[0].src);

   if (inot_instr == NULL || inot_instr->op != nir_op_inot)
      return false;

   /* HF is also possible as a destination on BDW+.  For nir_op_b2i, the set
    * of valid size-changing combinations is a bit more complex.
    *
    * The source restriction is just because I was lazy about generating the
    * constant below.
    */
   if (nir_dest_bit_size(instr->dest.dest) != 32 ||
       nir_src_bit_size(inot_instr->src[0].src) != 32)
      return false;

   /* b2[fi](inot(a)) maps a=0 => 1, a=-1 => 0.  Since a can only be 0 or -1,
    * this is float(1 + a).
    */
   fs_reg op;

   prepare_alu_destination_and_sources(bld, inot_instr, &op, false);

   /* Ignore the saturate modifier, if there is one.  The result of the
    * arithmetic can only be 0 or 1, so the clamping will do nothing anyway.
    */
   bld.ADD(result, op, brw_imm_d(1));

   return true;
}

/**
 * Emit code for nir_op_fsign possibly fused with a nir_op_fmul
 *
 * If \c instr is not the \c nir_op_fsign, then \c fsign_src is the index of
 * the source of \c instr that is a \c nir_op_fsign.
 */
void
fs_visitor::emit_fsign(const fs_builder &bld, const nir_alu_instr *instr,
                       fs_reg result, fs_reg *op, unsigned fsign_src)
{
   fs_inst *inst;

   assert(instr->op == nir_op_fsign || instr->op == nir_op_fmul);
   assert(fsign_src < nir_op_infos[instr->op].num_inputs);

   if (instr->op != nir_op_fsign) {
      const nir_alu_instr *const fsign_instr =
         nir_src_as_alu_instr(instr->src[fsign_src].src);

      /* op[fsign_src] has the nominal result of the fsign, and op[1 -
       * fsign_src] has the other multiply source.  This must be rearranged so
       * that op[0] is the source of the fsign op[1] is the other multiply
       * source.
       */
      if (fsign_src != 0)
         op[1] = op[0];

      op[0] = get_nir_src(fsign_instr->src[0].src);

      const nir_alu_type t =
         (nir_alu_type)(nir_op_infos[instr->op].input_types[0] |
                        nir_src_bit_size(fsign_instr->src[0].src));

      op[0].type = brw_type_for_nir_type(devinfo, t);

      unsigned channel = 0;
      if (nir_op_infos[instr->op].output_size == 0) {
         /* Since NIR is doing the scalarizing for us, we should only ever see
          * vectorized operations with a single channel.
          */
         assert(util_bitcount(instr->dest.write_mask) == 1);
         channel = ffs(instr->dest.write_mask) - 1;
      }

      op[0] = offset(op[0], bld, fsign_instr->src[0].swizzle[channel]);
   }

   if (type_sz(op[0].type) == 2) {
      /* AND(val, 0x8000) gives the sign bit.
       *
       * Predicated OR ORs 1.0 (0x3c00) with the sign bit if val is not zero.
       */
      fs_reg zero = retype(brw_imm_uw(0), BRW_REGISTER_TYPE_HF);
      bld.CMP(bld.null_reg_f(), op[0], zero, BRW_CONDITIONAL_NZ);

      op[0].type = BRW_REGISTER_TYPE_UW;
      result.type = BRW_REGISTER_TYPE_UW;
      bld.AND(result, op[0], brw_imm_uw(0x8000u));

      if (instr->op == nir_op_fsign)
         inst = bld.OR(result, result, brw_imm_uw(0x3c00u));
      else {
         /* Use XOR here to get the result sign correct. */
         inst = bld.XOR(result, result, retype(op[1], BRW_REGISTER_TYPE_UW));
      }

      inst->predicate = BRW_PREDICATE_NORMAL;
   } else if (type_sz(op[0].type) == 4) {
      /* AND(val, 0x80000000) gives the sign bit.
       *
       * Predicated OR ORs 1.0 (0x3f800000) with the sign bit if val is not
       * zero.
       */
      bld.CMP(bld.null_reg_f(), op[0], brw_imm_f(0.0f), BRW_CONDITIONAL_NZ);

      op[0].type = BRW_REGISTER_TYPE_UD;
      result.type = BRW_REGISTER_TYPE_UD;
      bld.AND(result, op[0], brw_imm_ud(0x80000000u));

      if (instr->op == nir_op_fsign)
         inst = bld.OR(result, result, brw_imm_ud(0x3f800000u));
      else {
         /* Use XOR here to get the result sign correct. */
         inst = bld.XOR(result, result, retype(op[1], BRW_REGISTER_TYPE_UD));
      }

      inst->predicate = BRW_PREDICATE_NORMAL;
   } else {
      /* For doubles we do the same but we need to consider:
       *
       * - 2-src instructions can't operate with 64-bit immediates
       * - The sign is encoded in the high 32-bit of each DF
       * - We need to produce a DF result.
       */

      fs_reg zero = vgrf(glsl_type::double_type);
      bld.MOV(zero, setup_imm_df(bld, 0.0));
      bld.CMP(bld.null_reg_df(), op[0], zero, BRW_CONDITIONAL_NZ);

      bld.MOV(result, zero);

      fs_reg r = subscript(result, BRW_REGISTER_TYPE_UD, 1);
      bld.AND(r, subscript(op[0], BRW_REGISTER_TYPE_UD, 1),
              brw_imm_ud(0x80000000u));

      if (instr->op == nir_op_fsign) {
         set_predicate(BRW_PREDICATE_NORMAL,
                       bld.OR(r, r, brw_imm_ud(0x3ff00000u)));
      } else {
         if (devinfo->has_64bit_int) {
            /* This could be done better in some cases.  If the scale is an
             * immediate with the low 32-bits all 0, emitting a separate XOR and
             * OR would allow an algebraic optimization to remove the OR.  There
             * are currently zero instances of fsign(double(x))*IMM in shader-db
             * or any test suite, so it is hard to care at this time.
             */
            fs_reg result_int64 = retype(result, BRW_REGISTER_TYPE_UQ);
            inst = bld.XOR(result_int64, result_int64,
                           retype(op[1], BRW_REGISTER_TYPE_UQ));
         } else {
            fs_reg result_int64 = retype(result, BRW_REGISTER_TYPE_UQ);
            bld.MOV(subscript(result_int64, BRW_REGISTER_TYPE_UD, 0),
                    subscript(op[1], BRW_REGISTER_TYPE_UD, 0));
            bld.XOR(subscript(result_int64, BRW_REGISTER_TYPE_UD, 1),
                    subscript(result_int64, BRW_REGISTER_TYPE_UD, 1),
                    subscript(op[1], BRW_REGISTER_TYPE_UD, 1));
         }
      }
   }
}

/**
 * Determine whether sources of a nir_op_fmul can be fused with a nir_op_fsign
 *
 * Checks the operands of a \c nir_op_fmul to determine whether or not
 * \c emit_fsign could fuse the multiplication with the \c sign() calculation.
 *
 * \param instr  The multiplication instruction
 *
 * \param fsign_src The source of \c instr that may or may not be a
 *                  \c nir_op_fsign
 */
static bool
can_fuse_fmul_fsign(nir_alu_instr *instr, unsigned fsign_src)
{
   assert(instr->op == nir_op_fmul);

   nir_alu_instr *const fsign_instr =
      nir_src_as_alu_instr(instr->src[fsign_src].src);

   /* Rules:
    *
    * 1. instr->src[fsign_src] must be a nir_op_fsign.
    * 2. The nir_op_fsign can only be used by this multiplication.
    * 3. The source that is the nir_op_fsign does not have source modifiers.
    *    \c emit_fsign only examines the source modifiers of the source of the
    *    \c nir_op_fsign.
    *
    * The nir_op_fsign must also not have the saturate modifier, but steps
    * have already been taken (in nir_opt_algebraic) to ensure that.
    */
   return fsign_instr != NULL && fsign_instr->op == nir_op_fsign &&
          is_used_once(fsign_instr);
}

static bool
is_const_zero(const nir_src &src)
{
   return nir_src_is_const(src) && nir_src_as_int(src) == 0;
}

void
fs_visitor::nir_emit_alu(const fs_builder &bld, nir_alu_instr *instr,
                         bool need_dest)
{
   fs_inst *inst;
   unsigned execution_mode =
      bld.shader->nir->info.float_controls_execution_mode;

   fs_reg op[NIR_MAX_VEC_COMPONENTS];
   fs_reg result = prepare_alu_destination_and_sources(bld, instr, op, need_dest);

#ifndef NDEBUG
   /* Everything except raw moves, some type conversions, iabs, and ineg
    * should have 8-bit sources lowered by nir_lower_bit_size in
    * brw_preprocess_nir or by brw_nir_lower_conversions in
    * brw_postprocess_nir.
    */
   switch (instr->op) {
   case nir_op_mov:
   case nir_op_vec2:
   case nir_op_vec3:
   case nir_op_vec4:
   case nir_op_vec8:
   case nir_op_vec16:
   case nir_op_i2f16:
   case nir_op_i2f32:
   case nir_op_i2i16:
   case nir_op_i2i32:
   case nir_op_u2f16:
   case nir_op_u2f32:
   case nir_op_u2u16:
   case nir_op_u2u32:
   case nir_op_iabs:
   case nir_op_ineg:
   case nir_op_pack_32_4x8_split:
      break;

   default:
      for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
         assert(type_sz(op[i].type) > 1);
      }
   }
#endif

   switch (instr->op) {
   case nir_op_mov:
   case nir_op_vec2:
   case nir_op_vec3:
   case nir_op_vec4:
   case nir_op_vec8:
   case nir_op_vec16: {
      fs_reg temp = result;
      bool need_extra_copy = false;
      for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
         if (!instr->src[i].src.is_ssa &&
             instr->dest.dest.reg.reg == instr->src[i].src.reg.reg) {
            need_extra_copy = true;
            temp = bld.vgrf(result.type, 4);
            break;
         }
      }

      unsigned last_bit = util_last_bit(instr->dest.write_mask);

      for (unsigned i = 0; i < last_bit; i++) {
         if (!(instr->dest.write_mask & (1 << i)))
            continue;

         if (instr->op == nir_op_mov) {
            bld.MOV(offset(temp, bld, i),
                           offset(op[0], bld, instr->src[0].swizzle[i]));
         } else {
            bld.MOV(offset(temp, bld, i),
                           offset(op[i], bld, instr->src[i].swizzle[0]));
         }
      }

      /* In this case the source and destination registers were the same,
       * so we need to insert an extra set of moves in order to deal with
       * any swizzling.
       */
      if (need_extra_copy) {
         for (unsigned i = 0; i < last_bit; i++) {
            if (!(instr->dest.write_mask & (1 << i)))
               continue;

            bld.MOV(offset(result, bld, i), offset(temp, bld, i));
         }
      }
      return;
   }

   case nir_op_i2f32:
   case nir_op_u2f32:
      if (optimize_extract_to_float(instr, result))
         return;
      inst = bld.MOV(result, op[0]);
      break;

   case nir_op_f2f16_rtne:
   case nir_op_f2f16_rtz:
   case nir_op_f2f16: {
      brw_rnd_mode rnd = BRW_RND_MODE_UNSPECIFIED;

      if (nir_op_f2f16 == instr->op)
         rnd = brw_rnd_mode_from_execution_mode(execution_mode);
      else
         rnd = brw_rnd_mode_from_nir_op(instr->op);

      if (BRW_RND_MODE_UNSPECIFIED != rnd)
         bld.exec_all().emit(SHADER_OPCODE_RND_MODE, bld.null_reg_ud(), brw_imm_d(rnd));

      assert(type_sz(op[0].type) < 8); /* brw_nir_lower_conversions */
      inst = bld.F32TO16(result, op[0]);
      break;
   }

   case nir_op_b2i8:
   case nir_op_b2i16:
   case nir_op_b2i32:
   case nir_op_b2i64:
   case nir_op_b2f16:
   case nir_op_b2f32:
   case nir_op_b2f64:
      if (try_emit_b2fi_of_inot(bld, result, instr))
         break;
      op[0].type = BRW_REGISTER_TYPE_D;
      op[0].negate = !op[0].negate;
      FALLTHROUGH;
   case nir_op_i2f64:
   case nir_op_i2i64:
   case nir_op_u2f64:
   case nir_op_u2u64:
   case nir_op_f2f64:
   case nir_op_f2i64:
   case nir_op_f2u64:
   case nir_op_i2i32:
   case nir_op_u2u32:
   case nir_op_f2i32:
   case nir_op_f2u32:
   case nir_op_i2f16:
   case nir_op_u2f16:
   case nir_op_f2i16:
   case nir_op_f2u16:
   case nir_op_f2i8:
   case nir_op_f2u8:
      if (result.type == BRW_REGISTER_TYPE_B ||
          result.type == BRW_REGISTER_TYPE_UB ||
          result.type == BRW_REGISTER_TYPE_HF)
         assert(type_sz(op[0].type) < 8); /* brw_nir_lower_conversions */

      if (op[0].type == BRW_REGISTER_TYPE_B ||
          op[0].type == BRW_REGISTER_TYPE_UB ||
          op[0].type == BRW_REGISTER_TYPE_HF)
         assert(type_sz(result.type) < 8); /* brw_nir_lower_conversions */

      inst = bld.MOV(result, op[0]);
      break;

   case nir_op_i2i8:
   case nir_op_u2u8:
      assert(type_sz(op[0].type) < 8); /* brw_nir_lower_conversions */
      FALLTHROUGH;
   case nir_op_i2i16:
   case nir_op_u2u16: {
      /* Emit better code for u2u8(extract_u8(a, b)) and similar patterns.
       * Emitting the instructions one by one results in two MOV instructions
       * that won't be propagated.  By handling both instructions here, a
       * single MOV is emitted.
       */
      nir_alu_instr *extract_instr = nir_src_as_alu_instr(instr->src[0].src);
      if (extract_instr != NULL) {
         if (extract_instr->op == nir_op_extract_u8 ||
             extract_instr->op == nir_op_extract_i8) {
            prepare_alu_destination_and_sources(bld, extract_instr, op, false);

            const unsigned byte = nir_src_as_uint(extract_instr->src[1].src);
            const brw_reg_type type =
               brw_int_type(1, extract_instr->op == nir_op_extract_i8);

            op[0] = subscript(op[0], type, byte);
         } else if (extract_instr->op == nir_op_extract_u16 ||
                    extract_instr->op == nir_op_extract_i16) {
            prepare_alu_destination_and_sources(bld, extract_instr, op, false);

            const unsigned word = nir_src_as_uint(extract_instr->src[1].src);
            const brw_reg_type type =
               brw_int_type(2, extract_instr->op == nir_op_extract_i16);

            op[0] = subscript(op[0], type, word);
         }
      }

      inst = bld.MOV(result, op[0]);
      break;
   }

   case nir_op_fsat:
      inst = bld.MOV(result, op[0]);
      inst->saturate = true;
      break;

   case nir_op_fneg:
   case nir_op_ineg:
      op[0].negate = true;
      inst = bld.MOV(result, op[0]);
      break;

   case nir_op_fabs:
   case nir_op_iabs:
      op[0].negate = false;
      op[0].abs = true;
      inst = bld.MOV(result, op[0]);
      break;

   case nir_op_f2f32:
      if (nir_has_any_rounding_mode_enabled(execution_mode)) {
         brw_rnd_mode rnd =
            brw_rnd_mode_from_execution_mode(execution_mode);
         bld.exec_all().emit(SHADER_OPCODE_RND_MODE, bld.null_reg_ud(),
                             brw_imm_d(rnd));
      }

      if (op[0].type == BRW_REGISTER_TYPE_HF)
         assert(type_sz(result.type) < 8); /* brw_nir_lower_conversions */

      inst = bld.MOV(result, op[0]);
      break;

   case nir_op_fsign:
      emit_fsign(bld, instr, result, op, 0);
      break;

   case nir_op_frcp:
      inst = bld.emit(SHADER_OPCODE_RCP, result, op[0]);
      break;

   case nir_op_fexp2:
      inst = bld.emit(SHADER_OPCODE_EXP2, result, op[0]);
      break;

   case nir_op_flog2:
      inst = bld.emit(SHADER_OPCODE_LOG2, result, op[0]);
      break;

   case nir_op_fsin:
      inst = bld.emit(SHADER_OPCODE_SIN, result, op[0]);
      break;

   case nir_op_fcos:
      inst = bld.emit(SHADER_OPCODE_COS, result, op[0]);
      break;

   case nir_op_fddx_fine:
      inst = bld.emit(FS_OPCODE_DDX_FINE, result, op[0]);
      break;
   case nir_op_fddx:
   case nir_op_fddx_coarse:
      inst = bld.emit(FS_OPCODE_DDX_COARSE, result, op[0]);
      break;
   case nir_op_fddy_fine:
      inst = bld.emit(FS_OPCODE_DDY_FINE, result, op[0]);
      break;
   case nir_op_fddy:
   case nir_op_fddy_coarse:
      inst = bld.emit(FS_OPCODE_DDY_COARSE, result, op[0]);
      break;

   case nir_op_fadd:
      if (nir_has_any_rounding_mode_enabled(execution_mode)) {
         brw_rnd_mode rnd =
            brw_rnd_mode_from_execution_mode(execution_mode);
         bld.exec_all().emit(SHADER_OPCODE_RND_MODE, bld.null_reg_ud(),
                             brw_imm_d(rnd));
      }
      FALLTHROUGH;
   case nir_op_iadd:
      inst = bld.ADD(result, op[0], op[1]);
      break;

   case nir_op_iadd3:
      inst = bld.ADD3(result, op[0], op[1], op[2]);
      break;

   case nir_op_iadd_sat:
   case nir_op_uadd_sat:
      inst = bld.ADD(result, op[0], op[1]);
      inst->saturate = true;
      break;

   case nir_op_isub_sat:
      bld.emit(SHADER_OPCODE_ISUB_SAT, result, op[0], op[1]);
      break;

   case nir_op_usub_sat:
      bld.emit(SHADER_OPCODE_USUB_SAT, result, op[0], op[1]);
      break;

   case nir_op_irhadd:
   case nir_op_urhadd:
      assert(nir_dest_bit_size(instr->dest.dest) < 64);
      inst = bld.AVG(result, op[0], op[1]);
      break;

   case nir_op_ihadd:
   case nir_op_uhadd: {
      assert(nir_dest_bit_size(instr->dest.dest) < 64);
      fs_reg tmp = bld.vgrf(result.type);

      if (devinfo->ver >= 8) {
         op[0] = resolve_source_modifiers(bld, op[0]);
         op[1] = resolve_source_modifiers(bld, op[1]);
      }

      /* AVG(x, y) - ((x ^ y) & 1) */
      bld.XOR(tmp, op[0], op[1]);
      bld.AND(tmp, tmp, retype(brw_imm_ud(1), result.type));
      bld.AVG(result, op[0], op[1]);
      inst = bld.ADD(result, result, tmp);
      inst->src[1].negate = true;
      break;
   }

   case nir_op_fmul:
      for (unsigned i = 0; i < 2; i++) {
         if (can_fuse_fmul_fsign(instr, i)) {
            emit_fsign(bld, instr, result, op, i);
            return;
         }
      }

      /* We emit the rounding mode after the previous fsign optimization since
       * it won't result in a MUL, but will try to negate the value by other
       * means.
       */
      if (nir_has_any_rounding_mode_enabled(execution_mode)) {
         brw_rnd_mode rnd =
            brw_rnd_mode_from_execution_mode(execution_mode);
         bld.exec_all().emit(SHADER_OPCODE_RND_MODE, bld.null_reg_ud(),
                             brw_imm_d(rnd));
      }

      inst = bld.MUL(result, op[0], op[1]);
      break;

   case nir_op_imul_2x32_64:
   case nir_op_umul_2x32_64:
      bld.MUL(result, op[0], op[1]);
      break;

   case nir_op_imul_32x16:
   case nir_op_umul_32x16: {
      const bool ud = instr->op == nir_op_umul_32x16;
      const enum brw_reg_type word_type =
         ud ? BRW_REGISTER_TYPE_UW : BRW_REGISTER_TYPE_W;
      const enum brw_reg_type dword_type =
         ud ? BRW_REGISTER_TYPE_UD : BRW_REGISTER_TYPE_D;

      assert(nir_dest_bit_size(instr->dest.dest) == 32);

      /* Before copy propagation there are no immediate values. */
      assert(op[0].file != IMM && op[1].file != IMM);

      op[1] = subscript(op[1], word_type, 0);

      if (devinfo->ver >= 7)
         bld.MUL(result, retype(op[0], dword_type), op[1]);
      else
         bld.MUL(result, op[1], retype(op[0], dword_type));

      break;
   }

   case nir_op_imul:
      assert(nir_dest_bit_size(instr->dest.dest) < 64);
      bld.MUL(result, op[0], op[1]);
      break;

   case nir_op_imul_high:
   case nir_op_umul_high:
      assert(nir_dest_bit_size(instr->dest.dest) < 64);
      if (nir_dest_bit_size(instr->dest.dest) == 32) {
         bld.emit(SHADER_OPCODE_MULH, result, op[0], op[1]);
      } else {
         fs_reg tmp = bld.vgrf(brw_reg_type_from_bit_size(32, op[0].type));
         bld.MUL(tmp, op[0], op[1]);
         bld.MOV(result, subscript(tmp, result.type, 1));
      }
      break;

   case nir_op_idiv:
   case nir_op_udiv:
      assert(nir_dest_bit_size(instr->dest.dest) < 64);
      bld.emit(SHADER_OPCODE_INT_QUOTIENT, result, op[0], op[1]);
      break;

   case nir_op_uadd_carry:
      unreachable("Should have been lowered by carry_to_arith().");

   case nir_op_usub_borrow:
      unreachable("Should have been lowered by borrow_to_arith().");

   case nir_op_umod:
   case nir_op_irem:
      /* According to the sign table for INT DIV in the Ivy Bridge PRM, it
       * appears that our hardware just does the right thing for signed
       * remainder.
       */
      assert(nir_dest_bit_size(instr->dest.dest) < 64);
      bld.emit(SHADER_OPCODE_INT_REMAINDER, result, op[0], op[1]);
      break;

   case nir_op_imod: {
      /* Get a regular C-style remainder.  If a % b == 0, set the predicate. */
      bld.emit(SHADER_OPCODE_INT_REMAINDER, result, op[0], op[1]);

      /* Math instructions don't support conditional mod */
      inst = bld.MOV(bld.null_reg_d(), result);
      inst->conditional_mod = BRW_CONDITIONAL_NZ;

      /* Now, we need to determine if signs of the sources are different.
       * When we XOR the sources, the top bit is 0 if they are the same and 1
       * if they are different.  We can then use a conditional modifier to
       * turn that into a predicate.  This leads us to an XOR.l instruction.
       *
       * Technically, according to the PRM, you're not allowed to use .l on a
       * XOR instruction.  However, empirical experiments and Curro's reading
       * of the simulator source both indicate that it's safe.
       */
      fs_reg tmp = bld.vgrf(BRW_REGISTER_TYPE_D);
      inst = bld.XOR(tmp, op[0], op[1]);
      inst->predicate = BRW_PREDICATE_NORMAL;
      inst->conditional_mod = BRW_CONDITIONAL_L;

      /* If the result of the initial remainder operation is non-zero and the
       * two sources have different signs, add in a copy of op[1] to get the
       * final integer modulus value.
       */
      inst = bld.ADD(result, result, op[1]);
      inst->predicate = BRW_PREDICATE_NORMAL;
      break;
   }

   case nir_op_flt32:
   case nir_op_fge32:
   case nir_op_feq32:
   case nir_op_fneu32: {
      fs_reg dest = result;

      const uint32_t bit_size =  nir_src_bit_size(instr->src[0].src);
      if (bit_size != 32) {
         dest = bld.vgrf(op[0].type, 1);
         bld.UNDEF(dest);
      }

      bld.CMP(dest, op[0], op[1], brw_cmod_for_nir_comparison(instr->op));

      if (bit_size > 32) {
         bld.MOV(result, subscript(dest, BRW_REGISTER_TYPE_UD, 0));
      } else if(bit_size < 32) {
         /* When we convert the result to 32-bit we need to be careful and do
          * it as a signed conversion to get sign extension (for 32-bit true)
          */
         const brw_reg_type src_type =
            brw_reg_type_from_bit_size(bit_size, BRW_REGISTER_TYPE_D);

         bld.MOV(retype(result, BRW_REGISTER_TYPE_D), retype(dest, src_type));
      }
      break;
   }

   case nir_op_ilt32:
   case nir_op_ult32:
   case nir_op_ige32:
   case nir_op_uge32:
   case nir_op_ieq32:
   case nir_op_ine32: {
      fs_reg dest = result;

      const uint32_t bit_size = type_sz(op[0].type) * 8;
      if (bit_size != 32) {
         dest = bld.vgrf(op[0].type, 1);
         bld.UNDEF(dest);
      }

      bld.CMP(dest, op[0], op[1],
              brw_cmod_for_nir_comparison(instr->op));

      if (bit_size > 32) {
         bld.MOV(result, subscript(dest, BRW_REGISTER_TYPE_UD, 0));
      } else if (bit_size < 32) {
         /* When we convert the result to 32-bit we need to be careful and do
          * it as a signed conversion to get sign extension (for 32-bit true)
          */
         const brw_reg_type src_type =
            brw_reg_type_from_bit_size(bit_size, BRW_REGISTER_TYPE_D);

         bld.MOV(retype(result, BRW_REGISTER_TYPE_D), retype(dest, src_type));
      }
      break;
   }

   case nir_op_inot:
      if (devinfo->ver >= 8) {
         nir_alu_instr *inot_src_instr = nir_src_as_alu_instr(instr->src[0].src);

         if (inot_src_instr != NULL &&
             (inot_src_instr->op == nir_op_ior ||
              inot_src_instr->op == nir_op_ixor ||
              inot_src_instr->op == nir_op_iand)) {
            /* The sources of the source logical instruction are now the
             * sources of the instruction that will be generated.
             */
            prepare_alu_destination_and_sources(bld, inot_src_instr, op, false);
            resolve_inot_sources(bld, inot_src_instr, op);

            /* Smash all of the sources and destination to be signed.  This
             * doesn't matter for the operation of the instruction, but cmod
             * propagation fails on unsigned sources with negation (due to
             * fs_inst::can_do_cmod returning false).
             */
            result.type =
               brw_type_for_nir_type(devinfo,
                                     (nir_alu_type)(nir_type_int |
                                                    nir_dest_bit_size(instr->dest.dest)));
            op[0].type =
               brw_type_for_nir_type(devinfo,
                                     (nir_alu_type)(nir_type_int |
                                                    nir_src_bit_size(inot_src_instr->src[0].src)));
            op[1].type =
               brw_type_for_nir_type(devinfo,
                                     (nir_alu_type)(nir_type_int |
                                                    nir_src_bit_size(inot_src_instr->src[1].src)));

            /* For XOR, only invert one of the sources.  Arbitrarily choose
             * the first source.
             */
            op[0].negate = !op[0].negate;
            if (inot_src_instr->op != nir_op_ixor)
               op[1].negate = !op[1].negate;

            switch (inot_src_instr->op) {
            case nir_op_ior:
               bld.AND(result, op[0], op[1]);
               return;

            case nir_op_iand:
               bld.OR(result, op[0], op[1]);
               return;

            case nir_op_ixor:
               bld.XOR(result, op[0], op[1]);
               return;

            default:
               unreachable("impossible opcode");
            }
         }
         op[0] = resolve_source_modifiers(bld, op[0]);
      }
      bld.NOT(result, op[0]);
      break;
   case nir_op_ixor:
      if (devinfo->ver >= 8) {
         resolve_inot_sources(bld, instr, op);
      }
      bld.XOR(result, op[0], op[1]);
      break;
   case nir_op_ior:
      if (devinfo->ver >= 8) {
         resolve_inot_sources(bld, instr, op);
      }
      bld.OR(result, op[0], op[1]);
      break;
   case nir_op_iand:
      if (devinfo->ver >= 8) {
         resolve_inot_sources(bld, instr, op);
      }
      bld.AND(result, op[0], op[1]);
      break;

   case nir_op_fdot2:
   case nir_op_fdot3:
   case nir_op_fdot4:
   case nir_op_b32all_fequal2:
   case nir_op_b32all_iequal2:
   case nir_op_b32all_fequal3:
   case nir_op_b32all_iequal3:
   case nir_op_b32all_fequal4:
   case nir_op_b32all_iequal4:
   case nir_op_b32any_fnequal2:
   case nir_op_b32any_inequal2:
   case nir_op_b32any_fnequal3:
   case nir_op_b32any_inequal3:
   case nir_op_b32any_fnequal4:
   case nir_op_b32any_inequal4:
      unreachable("Lowered by nir_lower_alu_reductions");

   case nir_op_ldexp:
      unreachable("not reached: should be handled by ldexp_to_arith()");

   case nir_op_fsqrt:
      inst = bld.emit(SHADER_OPCODE_SQRT, result, op[0]);
      break;

   case nir_op_frsq:
      inst = bld.emit(SHADER_OPCODE_RSQ, result, op[0]);
      break;

   case nir_op_ftrunc:
      inst = bld.RNDZ(result, op[0]);
      if (devinfo->ver < 6) {
         set_condmod(BRW_CONDITIONAL_R, inst);
         set_predicate(BRW_PREDICATE_NORMAL,
                       bld.ADD(result, result, brw_imm_f(1.0f)));
         inst = bld.MOV(result, result); /* for potential saturation */
      }
      break;

   case nir_op_fceil: {
      op[0].negate = !op[0].negate;
      fs_reg temp = vgrf(glsl_type::float_type);
      bld.RNDD(temp, op[0]);
      temp.negate = true;
      inst = bld.MOV(result, temp);
      break;
   }
   case nir_op_ffloor:
      inst = bld.RNDD(result, op[0]);
      break;
   case nir_op_ffract:
      inst = bld.FRC(result, op[0]);
      break;
   case nir_op_fround_even:
      inst = bld.RNDE(result, op[0]);
      if (devinfo->ver < 6) {
         set_condmod(BRW_CONDITIONAL_R, inst);
         set_predicate(BRW_PREDICATE_NORMAL,
                       bld.ADD(result, result, brw_imm_f(1.0f)));
         inst = bld.MOV(result, result); /* for potential saturation */
      }
      break;

   case nir_op_fquantize2f16: {
      fs_reg tmp16 = bld.vgrf(BRW_REGISTER_TYPE_D);
      fs_reg tmp32 = bld.vgrf(BRW_REGISTER_TYPE_F);
      fs_reg zero = bld.vgrf(BRW_REGISTER_TYPE_F);

      /* The destination stride must be at least as big as the source stride. */
      tmp16 = subscript(tmp16, BRW_REGISTER_TYPE_HF, 0);

      /* Check for denormal */
      fs_reg abs_src0 = op[0];
      abs_src0.abs = true;
      bld.CMP(bld.null_reg_f(), abs_src0, brw_imm_f(ldexpf(1.0, -14)),
              BRW_CONDITIONAL_L);
      /* Get the appropriately signed zero */
      bld.AND(retype(zero, BRW_REGISTER_TYPE_UD),
              retype(op[0], BRW_REGISTER_TYPE_UD),
              brw_imm_ud(0x80000000));
      /* Do the actual F32 -> F16 -> F32 conversion */
      bld.F32TO16(tmp16, op[0]);
      bld.F16TO32(tmp32, tmp16);
      /* Select that or zero based on normal status */
      inst = bld.SEL(result, zero, tmp32);
      inst->predicate = BRW_PREDICATE_NORMAL;
      break;
   }

   case nir_op_imin:
   case nir_op_umin:
   case nir_op_fmin:
      inst = bld.emit_minmax(result, op[0], op[1], BRW_CONDITIONAL_L);
      break;

   case nir_op_imax:
   case nir_op_umax:
   case nir_op_fmax:
      inst = bld.emit_minmax(result, op[0], op[1], BRW_CONDITIONAL_GE);
      break;

   case nir_op_pack_snorm_2x16:
   case nir_op_pack_snorm_4x8:
   case nir_op_pack_unorm_2x16:
   case nir_op_pack_unorm_4x8:
   case nir_op_unpack_snorm_2x16:
   case nir_op_unpack_snorm_4x8:
   case nir_op_unpack_unorm_2x16:
   case nir_op_unpack_unorm_4x8:
   case nir_op_unpack_half_2x16:
   case nir_op_pack_half_2x16:
      unreachable("not reached: should be handled by lower_packing_builtins");

   case nir_op_unpack_half_2x16_split_x_flush_to_zero:
      assert(FLOAT_CONTROLS_DENORM_FLUSH_TO_ZERO_FP16 & execution_mode);
      FALLTHROUGH;
   case nir_op_unpack_half_2x16_split_x:
      inst = bld.F16TO32(result, subscript(op[0], BRW_REGISTER_TYPE_HF, 0));
      break;

   case nir_op_unpack_half_2x16_split_y_flush_to_zero:
      assert(FLOAT_CONTROLS_DENORM_FLUSH_TO_ZERO_FP16 & execution_mode);
      FALLTHROUGH;
   case nir_op_unpack_half_2x16_split_y:
      inst = bld.F16TO32(result, subscript(op[0], BRW_REGISTER_TYPE_HF, 1));
      break;

   case nir_op_pack_64_2x32_split:
   case nir_op_pack_32_2x16_split:
      bld.emit(FS_OPCODE_PACK, result, op[0], op[1]);
      break;

   case nir_op_pack_32_4x8_split:
      bld.emit(FS_OPCODE_PACK, result, op, 4);
      break;

   case nir_op_unpack_64_2x32_split_x:
   case nir_op_unpack_64_2x32_split_y: {
      if (instr->op == nir_op_unpack_64_2x32_split_x)
         bld.MOV(result, subscript(op[0], BRW_REGISTER_TYPE_UD, 0));
      else
         bld.MOV(result, subscript(op[0], BRW_REGISTER_TYPE_UD, 1));
      break;
   }

   case nir_op_unpack_32_2x16_split_x:
   case nir_op_unpack_32_2x16_split_y: {
      if (instr->op == nir_op_unpack_32_2x16_split_x)
         bld.MOV(result, subscript(op[0], BRW_REGISTER_TYPE_UW, 0));
      else
         bld.MOV(result, subscript(op[0], BRW_REGISTER_TYPE_UW, 1));
      break;
   }

   case nir_op_fpow:
      inst = bld.emit(SHADER_OPCODE_POW, result, op[0], op[1]);
      break;

   case nir_op_bitfield_reverse:
      assert(nir_dest_bit_size(instr->dest.dest) == 32);
      assert(nir_src_bit_size(instr->src[0].src) == 32);
      bld.BFREV(result, op[0]);
      break;

   case nir_op_bit_count:
      assert(nir_dest_bit_size(instr->dest.dest) == 32);
      assert(nir_src_bit_size(instr->src[0].src) < 64);
      bld.CBIT(result, op[0]);
      break;

   case nir_op_uclz:
      assert(nir_dest_bit_size(instr->dest.dest) == 32);
      assert(nir_src_bit_size(instr->src[0].src) == 32);
      bld.LZD(retype(result, BRW_REGISTER_TYPE_UD), op[0]);
      break;

   case nir_op_ifind_msb: {
      assert(nir_dest_bit_size(instr->dest.dest) == 32);
      assert(nir_src_bit_size(instr->src[0].src) == 32);
      assert(devinfo->ver >= 7);

      bld.FBH(retype(result, BRW_REGISTER_TYPE_UD), op[0]);

      /* FBH counts from the MSB side, while GLSL's findMSB() wants the count
       * from the LSB side. If FBH didn't return an error (0xFFFFFFFF), then
       * subtract the result from 31 to convert the MSB count into an LSB
       * count.
       */
      bld.CMP(bld.null_reg_d(), result, brw_imm_d(-1), BRW_CONDITIONAL_NZ);

      inst = bld.ADD(result, result, brw_imm_d(31));
      inst->predicate = BRW_PREDICATE_NORMAL;
      inst->src[0].negate = true;
      break;
   }

   case nir_op_find_lsb:
      assert(nir_dest_bit_size(instr->dest.dest) == 32);
      assert(nir_src_bit_size(instr->src[0].src) == 32);
      assert(devinfo->ver >= 7);
      bld.FBL(result, op[0]);
      break;

   case nir_op_ubitfield_extract:
   case nir_op_ibitfield_extract:
      unreachable("should have been lowered");
   case nir_op_ubfe:
   case nir_op_ibfe:
      assert(nir_dest_bit_size(instr->dest.dest) < 64);
      bld.BFE(result, op[2], op[1], op[0]);
      break;
   case nir_op_bfm:
      assert(nir_dest_bit_size(instr->dest.dest) < 64);
      bld.BFI1(result, op[0], op[1]);
      break;
   case nir_op_bfi:
      assert(nir_dest_bit_size(instr->dest.dest) < 64);

      /* bfi is ((...) | (~src0 & src2)). The second part is zero when src2 is
       * either 0 or src0. Replacing the 0 with another value can eliminate a
       * temporary register.
       */
      if (is_const_zero(instr->src[2].src))
         bld.BFI2(result, op[0], op[1], op[0]);
      else
         bld.BFI2(result, op[0], op[1], op[2]);

      break;

   case nir_op_bitfield_insert:
      unreachable("not reached: should have been lowered");

   /* For all shift operations:
    *
    * Gen4 - Gen7: After application of source modifiers, the low 5-bits of
    * src1 are used an unsigned value for the shift count.
    *
    * Gen8: As with earlier platforms, but for Q and UQ types on src0, the low
    * 6-bit of src1 are used.
    *
    * Gen9+: The low bits of src1 matching the size of src0 (e.g., 4-bits for
    * W or UW src0).
    *
    * The implication is that the following instruction will produce a
    * different result on Gen9+ than on previous platforms:
    *
    *    shr(8)    g4<1>UW    g12<8,8,1>UW    0x0010UW
    *
    * where Gen9+ will shift by zero, and earlier platforms will shift by 16.
    *
    * This does not seem to be the case.  Experimentally, it has been
    * determined that shifts of 16-bit values on Gen8 behave properly.  Shifts
    * of 8-bit values on both Gen8 and Gen9 do not.  Gen11+ lowers 8-bit
    * values, so those platforms were not tested.  No features expose access
    * to 8- or 16-bit types on Gen7 or earlier, so those platforms were not
    * tested either.  See
    * https://gitlab.freedesktop.org/mesa/crucible/-/merge_requests/76.
    *
    * This is part of the reason 8-bit values are lowered to 16-bit on all
    * platforms.
    */
   case nir_op_ishl:
      bld.SHL(result, op[0], op[1]);
      break;
   case nir_op_ishr:
      bld.ASR(result, op[0], op[1]);
      break;
   case nir_op_ushr:
      bld.SHR(result, op[0], op[1]);
      break;

   case nir_op_urol:
      bld.ROL(result, op[0], op[1]);
      break;
   case nir_op_uror:
      bld.ROR(result, op[0], op[1]);
      break;

   case nir_op_pack_half_2x16_split:
      bld.emit(FS_OPCODE_PACK_HALF_2x16_SPLIT, result, op[0], op[1]);
      break;

   case nir_op_sdot_4x8_iadd:
   case nir_op_sdot_4x8_iadd_sat:
      inst = bld.DP4A(retype(result, BRW_REGISTER_TYPE_D),
                      retype(op[2], BRW_REGISTER_TYPE_D),
                      retype(op[0], BRW_REGISTER_TYPE_D),
                      retype(op[1], BRW_REGISTER_TYPE_D));

      if (instr->op == nir_op_sdot_4x8_iadd_sat)
         inst->saturate = true;
      break;

   case nir_op_udot_4x8_uadd:
   case nir_op_udot_4x8_uadd_sat:
      inst = bld.DP4A(retype(result, BRW_REGISTER_TYPE_UD),
                      retype(op[2], BRW_REGISTER_TYPE_UD),
                      retype(op[0], BRW_REGISTER_TYPE_UD),
                      retype(op[1], BRW_REGISTER_TYPE_UD));

      if (instr->op == nir_op_udot_4x8_uadd_sat)
         inst->saturate = true;
      break;

   case nir_op_sudot_4x8_iadd:
   case nir_op_sudot_4x8_iadd_sat:
      inst = bld.DP4A(retype(result, BRW_REGISTER_TYPE_D),
                      retype(op[2], BRW_REGISTER_TYPE_D),
                      retype(op[0], BRW_REGISTER_TYPE_D),
                      retype(op[1], BRW_REGISTER_TYPE_UD));

      if (instr->op == nir_op_sudot_4x8_iadd_sat)
         inst->saturate = true;
      break;

   case nir_op_ffma:
      if (nir_has_any_rounding_mode_enabled(execution_mode)) {
         brw_rnd_mode rnd =
            brw_rnd_mode_from_execution_mode(execution_mode);
         bld.exec_all().emit(SHADER_OPCODE_RND_MODE, bld.null_reg_ud(),
                             brw_imm_d(rnd));
      }

      inst = bld.MAD(result, op[2], op[1], op[0]);
      break;

   case nir_op_flrp:
      if (nir_has_any_rounding_mode_enabled(execution_mode)) {
         brw_rnd_mode rnd =
            brw_rnd_mode_from_execution_mode(execution_mode);
         bld.exec_all().emit(SHADER_OPCODE_RND_MODE, bld.null_reg_ud(),
                             brw_imm_d(rnd));
      }

      inst = bld.LRP(result, op[0], op[1], op[2]);
      break;

   case nir_op_b32csel:
      if (optimize_frontfacing_ternary(instr, result))
         return;

      bld.CMP(bld.null_reg_d(), op[0], brw_imm_d(0), BRW_CONDITIONAL_NZ);
      inst = bld.SEL(result, op[1], op[2]);
      inst->predicate = BRW_PREDICATE_NORMAL;
      break;

   case nir_op_extract_u8:
   case nir_op_extract_i8: {
      unsigned byte = nir_src_as_uint(instr->src[1].src);

      /* The PRMs say:
       *
       *    BDW+
       *    There is no direct conversion from B/UB to Q/UQ or Q/UQ to B/UB.
       *    Use two instructions and a word or DWord intermediate integer type.
       */
      if (nir_dest_bit_size(instr->dest.dest) == 64) {
         const brw_reg_type type = brw_int_type(1, instr->op == nir_op_extract_i8);

         if (instr->op == nir_op_extract_i8) {
            /* If we need to sign extend, extract to a word first */
            fs_reg w_temp = bld.vgrf(BRW_REGISTER_TYPE_W);
            bld.MOV(w_temp, subscript(op[0], type, byte));
            bld.MOV(result, w_temp);
         } else if (byte & 1) {
            /* Extract the high byte from the word containing the desired byte
             * offset.
             */
            bld.SHR(result,
                    subscript(op[0], BRW_REGISTER_TYPE_UW, byte / 2),
                    brw_imm_uw(8));
         } else {
            /* Otherwise use an AND with 0xff and a word type */
            bld.AND(result,
                    subscript(op[0], BRW_REGISTER_TYPE_UW, byte / 2),
                    brw_imm_uw(0xff));
         }
      } else {
         const brw_reg_type type = brw_int_type(1, instr->op == nir_op_extract_i8);
         bld.MOV(result, subscript(op[0], type, byte));
      }
      break;
   }

   case nir_op_extract_u16:
   case nir_op_extract_i16: {
      const brw_reg_type type = brw_int_type(2, instr->op == nir_op_extract_i16);
      unsigned word = nir_src_as_uint(instr->src[1].src);
      bld.MOV(result, subscript(op[0], type, word));
      break;
   }

   default:
      unreachable("unhandled instruction");
   }

   /* If we need to do a boolean resolve, replace the result with -(x & 1)
    * to sign extend the low bit to 0/~0
    */
   if (devinfo->ver <= 5 &&
       !result.is_null() &&
       (instr->instr.pass_flags & BRW_NIR_BOOLEAN_MASK) == BRW_NIR_BOOLEAN_NEEDS_RESOLVE) {
      fs_reg masked = vgrf(glsl_type::int_type);
      bld.AND(masked, result, brw_imm_d(1));
      masked.negate = true;
      bld.MOV(retype(result, BRW_REGISTER_TYPE_D), masked);
   }
}

void
fs_visitor::nir_emit_load_const(const fs_builder &bld,
                                nir_load_const_instr *instr)
{
   const brw_reg_type reg_type =
      brw_reg_type_from_bit_size(instr->def.bit_size, BRW_REGISTER_TYPE_D);
   fs_reg reg = bld.vgrf(reg_type, instr->def.num_components);

   switch (instr->def.bit_size) {
   case 8:
      for (unsigned i = 0; i < instr->def.num_components; i++)
         bld.MOV(offset(reg, bld, i), setup_imm_b(bld, instr->value[i].i8));
      break;

   case 16:
      for (unsigned i = 0; i < instr->def.num_components; i++)
         bld.MOV(offset(reg, bld, i), brw_imm_w(instr->value[i].i16));
      break;

   case 32:
      for (unsigned i = 0; i < instr->def.num_components; i++)
         bld.MOV(offset(reg, bld, i), brw_imm_d(instr->value[i].i32));
      break;

   case 64:
      assert(devinfo->ver >= 7);
      if (!devinfo->has_64bit_int) {
         for (unsigned i = 0; i < instr->def.num_components; i++) {
            bld.MOV(retype(offset(reg, bld, i), BRW_REGISTER_TYPE_DF),
                    setup_imm_df(bld, instr->value[i].f64));
         }
      } else {
         for (unsigned i = 0; i < instr->def.num_components; i++)
            bld.MOV(offset(reg, bld, i), brw_imm_q(instr->value[i].i64));
      }
      break;

   default:
      unreachable("Invalid bit size");
   }

   nir_ssa_values[instr->def.index] = reg;
}

bool
fs_visitor::get_nir_src_bindless(const nir_src &src)
{
   assert(src.is_ssa);

   return nir_ssa_bind_infos[src.ssa->index].bindless;
}

unsigned
fs_visitor::get_nir_src_block(const nir_src &src)
{
   assert(src.is_ssa);

   return nir_ssa_bind_infos[src.ssa->index].valid ?
          nir_ssa_bind_infos[src.ssa->index].block :
          UINT32_MAX;
}

static bool
is_resource_src(nir_src src)
{
   return src.is_ssa &&
          src.ssa->parent_instr->type == nir_instr_type_intrinsic &&
          nir_instr_as_intrinsic(src.ssa->parent_instr)->intrinsic == nir_intrinsic_resource_intel;
}

fs_reg
fs_visitor::get_resource_nir_src(const nir_src &src)
{
   if (!is_resource_src(src))
      return fs_reg();
   return nir_resource_values[src.ssa->index];
}

fs_reg
fs_visitor::get_nir_src(const nir_src &src)
{
   fs_reg reg;
   if (src.is_ssa) {
      if (nir_src_is_undef(src)) {
         const brw_reg_type reg_type =
            brw_reg_type_from_bit_size(src.ssa->bit_size, BRW_REGISTER_TYPE_D);
         reg = bld.vgrf(reg_type, src.ssa->num_components);
      } else {
         reg = nir_ssa_values[src.ssa->index];
      }
   } else {
      /* We don't handle indirects on locals */
      assert(src.reg.indirect == NULL);
      reg = offset(nir_locals[src.reg.reg->index], bld,
                   src.reg.base_offset * src.reg.reg->num_components);
   }

   if (nir_src_bit_size(src) == 64 && devinfo->ver == 7) {
      /* The only 64-bit type available on gfx7 is DF, so use that. */
      reg.type = BRW_REGISTER_TYPE_DF;
   } else {
      /* To avoid floating-point denorm flushing problems, set the type by
       * default to an integer type - instructions that need floating point
       * semantics will set this to F if they need to
       */
      reg.type = brw_reg_type_from_bit_size(nir_src_bit_size(src),
                                            BRW_REGISTER_TYPE_D);
   }

   return reg;
}

/**
 * Return an IMM for constants; otherwise call get_nir_src() as normal.
 *
 * This function should not be called on any value which may be 64 bits.
 * We could theoretically support 64-bit on gfx8+ but we choose not to
 * because it wouldn't work in general (no gfx7 support) and there are
 * enough restrictions in 64-bit immediates that you can't take the return
 * value and treat it the same as the result of get_nir_src().
 */
fs_reg
fs_visitor::get_nir_src_imm(const nir_src &src)
{
   assert(nir_src_bit_size(src) == 32);
   return nir_src_is_const(src) ?
          fs_reg(brw_imm_d(nir_src_as_int(src))) : get_nir_src(src);
}

fs_reg
fs_visitor::get_nir_dest(const nir_dest &dest)
{
   if (dest.is_ssa) {
      const brw_reg_type reg_type =
         brw_reg_type_from_bit_size(dest.ssa.bit_size,
                                    dest.ssa.bit_size == 8 ?
                                    BRW_REGISTER_TYPE_D :
                                    BRW_REGISTER_TYPE_F);
      nir_ssa_values[dest.ssa.index] =
         bld.vgrf(reg_type, dest.ssa.num_components);
      bld.UNDEF(nir_ssa_values[dest.ssa.index]);
      return nir_ssa_values[dest.ssa.index];
   } else {
      /* We don't handle indirects on locals */
      assert(dest.reg.indirect == NULL);
      return offset(nir_locals[dest.reg.reg->index], bld,
                    dest.reg.base_offset * dest.reg.reg->num_components);
   }
}

static fs_inst *
emit_pixel_interpolater_send(const fs_builder &bld,
                             enum opcode opcode,
                             const fs_reg &dst,
                             const fs_reg &src,
                             const fs_reg &desc,
                             glsl_interp_mode interpolation)
{
   struct brw_wm_prog_data *wm_prog_data =
      brw_wm_prog_data(bld.shader->stage_prog_data);

   fs_inst *inst = bld.emit(opcode, dst, src, desc);
   /* 2 floats per slot returned */
   inst->size_written = 2 * dst.component_size(inst->exec_size);
   if (interpolation == INTERP_MODE_NOPERSPECTIVE) {
      inst->pi_noperspective = true;
      /* TGL BSpec says:
       *     This field cannot be set to "Linear Interpolation"
       *     unless Non-Perspective Barycentric Enable in 3DSTATE_CLIP is enabled"
       */
      wm_prog_data->uses_nonperspective_interp_modes = true;
   }

   wm_prog_data->pulls_bary = true;

   return inst;
}

/**
 * Computes 1 << x, given a D/UD register containing some value x.
 */
static fs_reg
intexp2(const fs_builder &bld, const fs_reg &x)
{
   assert(x.type == BRW_REGISTER_TYPE_UD || x.type == BRW_REGISTER_TYPE_D);

   fs_reg result = bld.vgrf(x.type, 1);
   fs_reg one = bld.vgrf(x.type, 1);

   bld.MOV(one, retype(brw_imm_d(1), one.type));
   bld.SHL(result, one, x);
   return result;
}

void
fs_visitor::emit_gs_end_primitive(const nir_src &vertex_count_nir_src)
{
   assert(stage == MESA_SHADER_GEOMETRY);

   struct brw_gs_prog_data *gs_prog_data = brw_gs_prog_data(prog_data);

   if (gs_compile->control_data_header_size_bits == 0)
      return;

   /* We can only do EndPrimitive() functionality when the control data
    * consists of cut bits.  Fortunately, the only time it isn't is when the
    * output type is points, in which case EndPrimitive() is a no-op.
    */
   if (gs_prog_data->control_data_format !=
       GFX7_GS_CONTROL_DATA_FORMAT_GSCTL_CUT) {
      return;
   }

   /* Cut bits use one bit per vertex. */
   assert(gs_compile->control_data_bits_per_vertex == 1);

   fs_reg vertex_count = get_nir_src(vertex_count_nir_src);
   vertex_count.type = BRW_REGISTER_TYPE_UD;

   /* Cut bit n should be set to 1 if EndPrimitive() was called after emitting
    * vertex n, 0 otherwise.  So all we need to do here is mark bit
    * (vertex_count - 1) % 32 in the cut_bits register to indicate that
    * EndPrimitive() was called after emitting vertex (vertex_count - 1);
    * vec4_gs_visitor::emit_control_data_bits() will take care of the rest.
    *
    * Note that if EndPrimitive() is called before emitting any vertices, this
    * will cause us to set bit 31 of the control_data_bits register to 1.
    * That's fine because:
    *
    * - If max_vertices < 32, then vertex number 31 (zero-based) will never be
    *   output, so the hardware will ignore cut bit 31.
    *
    * - If max_vertices == 32, then vertex number 31 is guaranteed to be the
    *   last vertex, so setting cut bit 31 has no effect (since the primitive
    *   is automatically ended when the GS terminates).
    *
    * - If max_vertices > 32, then the ir_emit_vertex visitor will reset the
    *   control_data_bits register to 0 when the first vertex is emitted.
    */

   const fs_builder abld = bld.annotate("end primitive");

   /* control_data_bits |= 1 << ((vertex_count - 1) % 32) */
   fs_reg prev_count = bld.vgrf(BRW_REGISTER_TYPE_UD, 1);
   abld.ADD(prev_count, vertex_count, brw_imm_ud(0xffffffffu));
   fs_reg mask = intexp2(abld, prev_count);
   /* Note: we're relying on the fact that the GEN SHL instruction only pays
    * attention to the lower 5 bits of its second source argument, so on this
    * architecture, 1 << (vertex_count - 1) is equivalent to 1 <<
    * ((vertex_count - 1) % 32).
    */
   abld.OR(this->control_data_bits, this->control_data_bits, mask);
}

void
fs_visitor::emit_gs_control_data_bits(const fs_reg &vertex_count)
{
   assert(stage == MESA_SHADER_GEOMETRY);
   assert(gs_compile->control_data_bits_per_vertex != 0);

   struct brw_gs_prog_data *gs_prog_data = brw_gs_prog_data(prog_data);

   const fs_builder abld = bld.annotate("emit control data bits");
   const fs_builder fwa_bld = bld.exec_all();

   /* We use a single UD register to accumulate control data bits (32 bits
    * for each of the SIMD8 channels).  So we need to write a DWord (32 bits)
    * at a time.
    *
    * Unfortunately, the URB_WRITE_SIMD8 message uses 128-bit (OWord) offsets.
    * We have select a 128-bit group via the Global and Per-Slot Offsets, then
    * use the Channel Mask phase to enable/disable which DWord within that
    * group to write.  (Remember, different SIMD8 channels may have emitted
    * different numbers of vertices, so we may need per-slot offsets.)
    *
    * Channel masking presents an annoying problem: we may have to replicate
    * the data up to 4 times:
    *
    * Msg = Handles, Per-Slot Offsets, Channel Masks, Data, Data, Data, Data.
    *
    * To avoid penalizing shaders that emit a small number of vertices, we
    * can avoid these sometimes: if the size of the control data header is
    * <= 128 bits, then there is only 1 OWord.  All SIMD8 channels will land
    * land in the same 128-bit group, so we can skip per-slot offsets.
    *
    * Similarly, if the control data header is <= 32 bits, there is only one
    * DWord, so we can skip channel masks.
    */
   fs_reg channel_mask, per_slot_offset;

   if (gs_compile->control_data_header_size_bits > 32)
      channel_mask = vgrf(glsl_type::uint_type);

   if (gs_compile->control_data_header_size_bits > 128)
      per_slot_offset = vgrf(glsl_type::uint_type);

   /* Figure out which DWord we're trying to write to using the formula:
    *
    *    dword_index = (vertex_count - 1) * bits_per_vertex / 32
    *
    * Since bits_per_vertex is a power of two, and is known at compile
    * time, this can be optimized to:
    *
    *    dword_index = (vertex_count - 1) >> (6 - log2(bits_per_vertex))
    */
   if (channel_mask.file != BAD_FILE || per_slot_offset.file != BAD_FILE) {
      fs_reg dword_index = bld.vgrf(BRW_REGISTER_TYPE_UD, 1);
      fs_reg prev_count = bld.vgrf(BRW_REGISTER_TYPE_UD, 1);
      abld.ADD(prev_count, vertex_count, brw_imm_ud(0xffffffffu));
      unsigned log2_bits_per_vertex =
         util_last_bit(gs_compile->control_data_bits_per_vertex);
      abld.SHR(dword_index, prev_count, brw_imm_ud(6u - log2_bits_per_vertex));

      if (per_slot_offset.file != BAD_FILE) {
         /* Set the per-slot offset to dword_index / 4, so that we'll write to
          * the appropriate OWord within the control data header.
          */
         abld.SHR(per_slot_offset, dword_index, brw_imm_ud(2u));
      }

      /* Set the channel masks to 1 << (dword_index % 4), so that we'll
       * write to the appropriate DWORD within the OWORD.
       */
      fs_reg channel = bld.vgrf(BRW_REGISTER_TYPE_UD, 1);
      fwa_bld.AND(channel, dword_index, brw_imm_ud(3u));
      channel_mask = intexp2(fwa_bld, channel);
      /* Then the channel masks need to be in bits 23:16. */
      fwa_bld.SHL(channel_mask, channel_mask, brw_imm_ud(16u));
   }

   /* Store the control data bits in the message payload and send it. */
   const unsigned header_size = 1 + unsigned(channel_mask.file != BAD_FILE) +
      unsigned(per_slot_offset.file != BAD_FILE);

   /* If there are channel masks, add 3 extra copies of the data. */
   const unsigned length = 1 + 3 * unsigned(channel_mask.file != BAD_FILE);

   fs_reg sources[4];

   for (unsigned i = 0; i < ARRAY_SIZE(sources); i++)
      sources[i] = this->control_data_bits;

   fs_reg srcs[URB_LOGICAL_NUM_SRCS];
   srcs[URB_LOGICAL_SRC_HANDLE] = gs_payload().urb_handles;
   srcs[URB_LOGICAL_SRC_PER_SLOT_OFFSETS] = per_slot_offset;
   srcs[URB_LOGICAL_SRC_CHANNEL_MASK] = channel_mask;
   srcs[URB_LOGICAL_SRC_DATA] = bld.vgrf(BRW_REGISTER_TYPE_F, length);
   abld.LOAD_PAYLOAD(srcs[URB_LOGICAL_SRC_DATA], sources, length, 0);

   fs_inst *inst = abld.emit(SHADER_OPCODE_URB_WRITE_LOGICAL, reg_undef,
                             srcs, ARRAY_SIZE(srcs));
   inst->mlen = header_size + length;
   /* We need to increment Global Offset by 256-bits to make room for
    * Broadwell's extra "Vertex Count" payload at the beginning of the
    * URB entry.  Since this is an OWord message, Global Offset is counted
    * in 128-bit units, so we must set it to 2.
    */
   if (gs_prog_data->static_vertex_count == -1)
      inst->offset = 2;
}

void
fs_visitor::set_gs_stream_control_data_bits(const fs_reg &vertex_count,
                                            unsigned stream_id)
{
   /* control_data_bits |= stream_id << ((2 * (vertex_count - 1)) % 32) */

   /* Note: we are calling this *before* increasing vertex_count, so
    * this->vertex_count == vertex_count - 1 in the formula above.
    */

   /* Stream mode uses 2 bits per vertex */
   assert(gs_compile->control_data_bits_per_vertex == 2);

   /* Must be a valid stream */
   assert(stream_id < MAX_VERTEX_STREAMS);

   /* Control data bits are initialized to 0 so we don't have to set any
    * bits when sending vertices to stream 0.
    */
   if (stream_id == 0)
      return;

   const fs_builder abld = bld.annotate("set stream control data bits", NULL);

   /* reg::sid = stream_id */
   fs_reg sid = bld.vgrf(BRW_REGISTER_TYPE_UD, 1);
   abld.MOV(sid, brw_imm_ud(stream_id));

   /* reg:shift_count = 2 * (vertex_count - 1) */
   fs_reg shift_count = bld.vgrf(BRW_REGISTER_TYPE_UD, 1);
   abld.SHL(shift_count, vertex_count, brw_imm_ud(1u));

   /* Note: we're relying on the fact that the GEN SHL instruction only pays
    * attention to the lower 5 bits of its second source argument, so on this
    * architecture, stream_id << 2 * (vertex_count - 1) is equivalent to
    * stream_id << ((2 * (vertex_count - 1)) % 32).
    */
   fs_reg mask = bld.vgrf(BRW_REGISTER_TYPE_UD, 1);
   abld.SHL(mask, sid, shift_count);
   abld.OR(this->control_data_bits, this->control_data_bits, mask);
}

void
fs_visitor::emit_gs_vertex(const nir_src &vertex_count_nir_src,
                           unsigned stream_id)
{
   assert(stage == MESA_SHADER_GEOMETRY);

   struct brw_gs_prog_data *gs_prog_data = brw_gs_prog_data(prog_data);

   fs_reg vertex_count = get_nir_src(vertex_count_nir_src);
   vertex_count.type = BRW_REGISTER_TYPE_UD;

   /* Haswell and later hardware ignores the "Render Stream Select" bits
    * from the 3DSTATE_STREAMOUT packet when the SOL stage is disabled,
    * and instead sends all primitives down the pipeline for rasterization.
    * If the SOL stage is enabled, "Render Stream Select" is honored and
    * primitives bound to non-zero streams are discarded after stream output.
    *
    * Since the only purpose of primives sent to non-zero streams is to
    * be recorded by transform feedback, we can simply discard all geometry
    * bound to these streams when transform feedback is disabled.
    */
   if (stream_id > 0 && !nir->info.has_transform_feedback_varyings)
      return;

   /* If we're outputting 32 control data bits or less, then we can wait
    * until the shader is over to output them all.  Otherwise we need to
    * output them as we go.  Now is the time to do it, since we're about to
    * output the vertex_count'th vertex, so it's guaranteed that the
    * control data bits associated with the (vertex_count - 1)th vertex are
    * correct.
    */
   if (gs_compile->control_data_header_size_bits > 32) {
      const fs_builder abld =
         bld.annotate("emit vertex: emit control data bits");

      /* Only emit control data bits if we've finished accumulating a batch
       * of 32 bits.  This is the case when:
       *
       *     (vertex_count * bits_per_vertex) % 32 == 0
       *
       * (in other words, when the last 5 bits of vertex_count *
       * bits_per_vertex are 0).  Assuming bits_per_vertex == 2^n for some
       * integer n (which is always the case, since bits_per_vertex is
       * always 1 or 2), this is equivalent to requiring that the last 5-n
       * bits of vertex_count are 0:
       *
       *     vertex_count & (2^(5-n) - 1) == 0
       *
       * 2^(5-n) == 2^5 / 2^n == 32 / bits_per_vertex, so this is
       * equivalent to:
       *
       *     vertex_count & (32 / bits_per_vertex - 1) == 0
       *
       * TODO: If vertex_count is an immediate, we could do some of this math
       *       at compile time...
       */
      fs_inst *inst =
         abld.AND(bld.null_reg_d(), vertex_count,
                  brw_imm_ud(32u / gs_compile->control_data_bits_per_vertex - 1u));
      inst->conditional_mod = BRW_CONDITIONAL_Z;

      abld.IF(BRW_PREDICATE_NORMAL);
      /* If vertex_count is 0, then no control data bits have been
       * accumulated yet, so we can skip emitting them.
       */
      abld.CMP(bld.null_reg_d(), vertex_count, brw_imm_ud(0u),
               BRW_CONDITIONAL_NEQ);
      abld.IF(BRW_PREDICATE_NORMAL);
      emit_gs_control_data_bits(vertex_count);
      abld.emit(BRW_OPCODE_ENDIF);

      /* Reset control_data_bits to 0 so we can start accumulating a new
       * batch.
       *
       * Note: in the case where vertex_count == 0, this neutralizes the
       * effect of any call to EndPrimitive() that the shader may have
       * made before outputting its first vertex.
       */
      inst = abld.MOV(this->control_data_bits, brw_imm_ud(0u));
      inst->force_writemask_all = true;
      abld.emit(BRW_OPCODE_ENDIF);
   }

   emit_urb_writes(vertex_count);

   /* In stream mode we have to set control data bits for all vertices
    * unless we have disabled control data bits completely (which we do
    * do for GL_POINTS outputs that don't use streams).
    */
   if (gs_compile->control_data_header_size_bits > 0 &&
       gs_prog_data->control_data_format ==
          GFX7_GS_CONTROL_DATA_FORMAT_GSCTL_SID) {
      set_gs_stream_control_data_bits(vertex_count, stream_id);
   }
}

void
fs_visitor::emit_gs_input_load(const fs_reg &dst,
                               const nir_src &vertex_src,
                               unsigned base_offset,
                               const nir_src &offset_src,
                               unsigned num_components,
                               unsigned first_component)
{
   assert(type_sz(dst.type) == 4);
   struct brw_gs_prog_data *gs_prog_data = brw_gs_prog_data(prog_data);
   const unsigned push_reg_count = gs_prog_data->base.urb_read_length * 8;

   /* TODO: figure out push input layout for invocations == 1 */
   if (gs_prog_data->invocations == 1 &&
       nir_src_is_const(offset_src) && nir_src_is_const(vertex_src) &&
       4 * (base_offset + nir_src_as_uint(offset_src)) < push_reg_count) {
      int imm_offset = (base_offset + nir_src_as_uint(offset_src)) * 4 +
                       nir_src_as_uint(vertex_src) * push_reg_count;
      for (unsigned i = 0; i < num_components; i++) {
         bld.MOV(offset(dst, bld, i),
                 fs_reg(ATTR, imm_offset + i + first_component, dst.type));
      }
      return;
   }

   /* Resort to the pull model.  Ensure the VUE handles are provided. */
   assert(gs_prog_data->base.include_vue_handles);

   fs_reg start = gs_payload().icp_handle_start;
   fs_reg icp_handle = bld.vgrf(BRW_REGISTER_TYPE_UD, 1);

   if (gs_prog_data->invocations == 1) {
      if (nir_src_is_const(vertex_src)) {
         /* The vertex index is constant; just select the proper URB handle. */
         icp_handle = offset(start, bld, nir_src_as_uint(vertex_src));
      } else {
         /* The vertex index is non-constant.  We need to use indirect
          * addressing to fetch the proper URB handle.
          *
          * First, we start with the sequence <7, 6, 5, 4, 3, 2, 1, 0>
          * indicating that channel <n> should read the handle from
          * DWord <n>.  We convert that to bytes by multiplying by 4.
          *
          * Next, we convert the vertex index to bytes by multiplying
          * by 32 (shifting by 5), and add the two together.  This is
          * the final indirect byte offset.
          */
         fs_reg sequence =
            nir_system_values[SYSTEM_VALUE_SUBGROUP_INVOCATION];
         fs_reg channel_offsets = bld.vgrf(BRW_REGISTER_TYPE_UD, 1);
         fs_reg vertex_offset_bytes = bld.vgrf(BRW_REGISTER_TYPE_UD, 1);
         fs_reg icp_offset_bytes = bld.vgrf(BRW_REGISTER_TYPE_UD, 1);

         /* channel_offsets = 4 * sequence = <28, 24, 20, 16, 12, 8, 4, 0> */
         bld.SHL(channel_offsets, sequence, brw_imm_ud(2u));
         /* Convert vertex_index to bytes (multiply by 32) */
         bld.SHL(vertex_offset_bytes,
                 retype(get_nir_src(vertex_src), BRW_REGISTER_TYPE_UD),
                 brw_imm_ud(5u));
         bld.ADD(icp_offset_bytes, vertex_offset_bytes, channel_offsets);

         /* Use first_icp_handle as the base offset.  There is one register
          * of URB handles per vertex, so inform the register allocator that
          * we might read up to nir->info.gs.vertices_in registers.
          */
         bld.emit(SHADER_OPCODE_MOV_INDIRECT, icp_handle, start,
                  fs_reg(icp_offset_bytes),
                  brw_imm_ud(nir->info.gs.vertices_in * REG_SIZE));
      }
   } else {
      assert(gs_prog_data->invocations > 1);

      if (nir_src_is_const(vertex_src)) {
         unsigned vertex = nir_src_as_uint(vertex_src);
         assert(devinfo->ver >= 9 || vertex <= 5);
         bld.MOV(icp_handle, component(start, vertex));
      } else {
         /* The vertex index is non-constant.  We need to use indirect
          * addressing to fetch the proper URB handle.
          *
          */
         fs_reg icp_offset_bytes = bld.vgrf(BRW_REGISTER_TYPE_UD, 1);

         /* Convert vertex_index to bytes (multiply by 4) */
         bld.SHL(icp_offset_bytes,
                 retype(get_nir_src(vertex_src), BRW_REGISTER_TYPE_UD),
                 brw_imm_ud(2u));

         /* Use first_icp_handle as the base offset.  There is one DWord
          * of URB handles per vertex, so inform the register allocator that
          * we might read up to ceil(nir->info.gs.vertices_in / 8) registers.
          */
         bld.emit(SHADER_OPCODE_MOV_INDIRECT, icp_handle, start,
                  fs_reg(icp_offset_bytes),
                  brw_imm_ud(DIV_ROUND_UP(nir->info.gs.vertices_in, 8) *
                             REG_SIZE));
      }
   }

   fs_inst *inst;
   fs_reg indirect_offset = get_nir_src(offset_src);

   if (nir_src_is_const(offset_src)) {
      fs_reg srcs[URB_LOGICAL_NUM_SRCS];
      srcs[URB_LOGICAL_SRC_HANDLE] = icp_handle;

      /* Constant indexing - use global offset. */
      if (first_component != 0) {
         unsigned read_components = num_components + first_component;
         fs_reg tmp = bld.vgrf(dst.type, read_components);
         inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, tmp, srcs,
                         ARRAY_SIZE(srcs));
         inst->size_written = read_components *
                              tmp.component_size(inst->exec_size);
         for (unsigned i = 0; i < num_components; i++) {
            bld.MOV(offset(dst, bld, i),
                    offset(tmp, bld, i + first_component));
         }
      } else {
         inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, dst, srcs,
                         ARRAY_SIZE(srcs));
         inst->size_written = num_components *
                              dst.component_size(inst->exec_size);
      }
      inst->offset = base_offset + nir_src_as_uint(offset_src);
      inst->mlen = 1;
   } else {
      /* Indirect indexing - use per-slot offsets as well. */
      unsigned read_components = num_components + first_component;
      fs_reg tmp = bld.vgrf(dst.type, read_components);

      fs_reg srcs[URB_LOGICAL_NUM_SRCS];
      srcs[URB_LOGICAL_SRC_HANDLE] = icp_handle;
      srcs[URB_LOGICAL_SRC_PER_SLOT_OFFSETS] = indirect_offset;

      if (first_component != 0) {
         inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, tmp,
                         srcs, ARRAY_SIZE(srcs));
         inst->size_written = read_components *
                              tmp.component_size(inst->exec_size);
         for (unsigned i = 0; i < num_components; i++) {
            bld.MOV(offset(dst, bld, i),
                    offset(tmp, bld, i + first_component));
         }
      } else {
         inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, dst,
                         srcs, ARRAY_SIZE(srcs));
         inst->size_written = num_components *
                              dst.component_size(inst->exec_size);
      }
      inst->offset = base_offset;
      inst->mlen = 2;
   }
}

fs_reg
fs_visitor::get_indirect_offset(nir_intrinsic_instr *instr)
{
   nir_src *offset_src = nir_get_io_offset_src(instr);

   if (nir_src_is_const(*offset_src)) {
      /* The only constant offset we should find is 0.  brw_nir.c's
       * add_const_offset_to_base() will fold other constant offsets
       * into the "base" index.
       */
      assert(nir_src_as_uint(*offset_src) == 0);
      return fs_reg();
   }

   return get_nir_src(*offset_src);
}

void
fs_visitor::nir_emit_vs_intrinsic(const fs_builder &bld,
                                  nir_intrinsic_instr *instr)
{
   assert(stage == MESA_SHADER_VERTEX);

   fs_reg dest;
   if (nir_intrinsic_infos[instr->intrinsic].has_dest)
      dest = get_nir_dest(instr->dest);

   switch (instr->intrinsic) {
   case nir_intrinsic_load_vertex_id:
   case nir_intrinsic_load_base_vertex:
      unreachable("should be lowered by nir_lower_system_values()");

   case nir_intrinsic_load_input: {
      assert(nir_dest_bit_size(instr->dest) == 32);
      fs_reg src = fs_reg(ATTR, nir_intrinsic_base(instr) * 4, dest.type);
      src = offset(src, bld, nir_intrinsic_component(instr));
      src = offset(src, bld, nir_src_as_uint(instr->src[0]));

      for (unsigned i = 0; i < instr->num_components; i++)
         bld.MOV(offset(dest, bld, i), offset(src, bld, i));
      break;
   }

   case nir_intrinsic_load_vertex_id_zero_base:
   case nir_intrinsic_load_instance_id:
   case nir_intrinsic_load_base_instance:
   case nir_intrinsic_load_draw_id:
   case nir_intrinsic_load_first_vertex:
   case nir_intrinsic_load_is_indexed_draw:
      unreachable("lowered by brw_nir_lower_vs_inputs");

   default:
      nir_emit_intrinsic(bld, instr);
      break;
   }
}

fs_reg
fs_visitor::get_tcs_single_patch_icp_handle(const fs_builder &bld,
                                            nir_intrinsic_instr *instr)
{
   struct brw_tcs_prog_data *tcs_prog_data = brw_tcs_prog_data(prog_data);
   const nir_src &vertex_src = instr->src[0];
   nir_intrinsic_instr *vertex_intrin = nir_src_as_intrinsic(vertex_src);

   const fs_reg start = tcs_payload().icp_handle_start;

   fs_reg icp_handle;

   if (nir_src_is_const(vertex_src)) {
      /* Emit a MOV to resolve <0,1,0> regioning. */
      icp_handle = bld.vgrf(BRW_REGISTER_TYPE_UD, 1);
      unsigned vertex = nir_src_as_uint(vertex_src);
      bld.MOV(icp_handle, component(start, vertex));
   } else if (tcs_prog_data->instances == 1 && vertex_intrin &&
              vertex_intrin->intrinsic == nir_intrinsic_load_invocation_id) {
      /* For the common case of only 1 instance, an array index of
       * gl_InvocationID means reading the handles from the start.  Skip all
       * the indirect work.
       */
      icp_handle = start;
   } else {
      /* The vertex index is non-constant.  We need to use indirect
       * addressing to fetch the proper URB handle.
       */
      icp_handle = bld.vgrf(BRW_REGISTER_TYPE_UD, 1);

      /* Each ICP handle is a single DWord (4 bytes) */
      fs_reg vertex_offset_bytes = bld.vgrf(BRW_REGISTER_TYPE_UD, 1);
      bld.SHL(vertex_offset_bytes,
              retype(get_nir_src(vertex_src), BRW_REGISTER_TYPE_UD),
              brw_imm_ud(2u));

      /* We might read up to 4 registers. */
      bld.emit(SHADER_OPCODE_MOV_INDIRECT, icp_handle,
               start, vertex_offset_bytes,
               brw_imm_ud(4 * REG_SIZE));
   }

   return icp_handle;
}

fs_reg
fs_visitor::get_tcs_multi_patch_icp_handle(const fs_builder &bld,
                                           nir_intrinsic_instr *instr)
{
   struct brw_tcs_prog_key *tcs_key = (struct brw_tcs_prog_key *) key;
   const nir_src &vertex_src = instr->src[0];

   const fs_reg start = tcs_payload().icp_handle_start;

   if (nir_src_is_const(vertex_src))
      return byte_offset(start, nir_src_as_uint(vertex_src) * REG_SIZE);

   /* The vertex index is non-constant.  We need to use indirect
    * addressing to fetch the proper URB handle.
    *
    * First, we start with the sequence <7, 6, 5, 4, 3, 2, 1, 0>
    * indicating that channel <n> should read the handle from
    * DWord <n>.  We convert that to bytes by multiplying by 4.
    *
    * Next, we convert the vertex index to bytes by multiplying
    * by 32 (shifting by 5), and add the two together.  This is
    * the final indirect byte offset.
    */
   fs_reg icp_handle = bld.vgrf(BRW_REGISTER_TYPE_UD, 1);
   fs_reg sequence = nir_system_values[SYSTEM_VALUE_SUBGROUP_INVOCATION];
   fs_reg channel_offsets = bld.vgrf(BRW_REGISTER_TYPE_UD, 1);
   fs_reg vertex_offset_bytes = bld.vgrf(BRW_REGISTER_TYPE_UD, 1);
   fs_reg icp_offset_bytes = bld.vgrf(BRW_REGISTER_TYPE_UD, 1);

   /* channel_offsets = 4 * sequence = <28, 24, 20, 16, 12, 8, 4, 0> */
   bld.SHL(channel_offsets, sequence, brw_imm_ud(2u));
   /* Convert vertex_index to bytes (multiply by 32) */
   bld.SHL(vertex_offset_bytes,
           retype(get_nir_src(vertex_src), BRW_REGISTER_TYPE_UD),
           brw_imm_ud(5u));
   bld.ADD(icp_offset_bytes, vertex_offset_bytes, channel_offsets);

   /* Use start of ICP handles as the base offset.  There is one register
    * of URB handles per vertex, so inform the register allocator that
    * we might read up to nir->info.gs.vertices_in registers.
    */
   bld.emit(SHADER_OPCODE_MOV_INDIRECT, icp_handle, start,
            icp_offset_bytes,
            brw_imm_ud(brw_tcs_prog_key_input_vertices(tcs_key) * REG_SIZE));

   return icp_handle;
}

void
fs_visitor::nir_emit_tcs_intrinsic(const fs_builder &bld,
                                   nir_intrinsic_instr *instr)
{
   assert(stage == MESA_SHADER_TESS_CTRL);
   struct brw_tcs_prog_data *tcs_prog_data = brw_tcs_prog_data(prog_data);
   struct brw_vue_prog_data *vue_prog_data = &tcs_prog_data->base;

   fs_reg dst;
   if (nir_intrinsic_infos[instr->intrinsic].has_dest)
      dst = get_nir_dest(instr->dest);

   switch (instr->intrinsic) {
   case nir_intrinsic_load_primitive_id:
      bld.MOV(dst, tcs_payload().primitive_id);
      break;
   case nir_intrinsic_load_invocation_id:
      bld.MOV(retype(dst, invocation_id.type), invocation_id);
      break;

   case nir_intrinsic_scoped_barrier:
      if (nir_intrinsic_memory_scope(instr) != SCOPE_NONE)
         nir_emit_intrinsic(bld, instr);
      if (nir_intrinsic_execution_scope(instr) == SCOPE_WORKGROUP) {
         if (tcs_prog_data->instances != 1)
            emit_tcs_barrier();
      }
      break;

   case nir_intrinsic_load_input:
      unreachable("nir_lower_io should never give us these.");
      break;

   case nir_intrinsic_load_per_vertex_input: {
      assert(nir_dest_bit_size(instr->dest) == 32);
      fs_reg indirect_offset = get_indirect_offset(instr);
      unsigned imm_offset = nir_intrinsic_base(instr);
      fs_inst *inst;

      const bool multi_patch =
         vue_prog_data->dispatch_mode == DISPATCH_MODE_TCS_MULTI_PATCH;

      fs_reg icp_handle = multi_patch ?
         get_tcs_multi_patch_icp_handle(bld, instr) :
         get_tcs_single_patch_icp_handle(bld, instr);

      /* We can only read two double components with each URB read, so
       * we send two read messages in that case, each one loading up to
       * two double components.
       */
      unsigned num_components = instr->num_components;
      unsigned first_component = nir_intrinsic_component(instr);

      fs_reg srcs[URB_LOGICAL_NUM_SRCS];
      srcs[URB_LOGICAL_SRC_HANDLE] = icp_handle;

      if (indirect_offset.file == BAD_FILE) {
         /* Constant indexing - use global offset. */
         if (first_component != 0) {
            unsigned read_components = num_components + first_component;
            fs_reg tmp = bld.vgrf(dst.type, read_components);
            inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, tmp, srcs,
                            ARRAY_SIZE(srcs));
            for (unsigned i = 0; i < num_components; i++) {
               bld.MOV(offset(dst, bld, i),
                       offset(tmp, bld, i + first_component));
            }
         } else {
            inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, dst, srcs,
                            ARRAY_SIZE(srcs));
         }
         inst->offset = imm_offset;
         inst->mlen = 1;
      } else {
         /* Indirect indexing - use per-slot offsets as well. */
         srcs[URB_LOGICAL_SRC_PER_SLOT_OFFSETS] = indirect_offset;

         if (first_component != 0) {
            unsigned read_components = num_components + first_component;
            fs_reg tmp = bld.vgrf(dst.type, read_components);
            inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, tmp,
                            srcs, ARRAY_SIZE(srcs));
            for (unsigned i = 0; i < num_components; i++) {
               bld.MOV(offset(dst, bld, i),
                       offset(tmp, bld, i + first_component));
            }
         } else {
            inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, dst,
                            srcs, ARRAY_SIZE(srcs));
         }
         inst->offset = imm_offset;
         inst->mlen = 2;
      }
      inst->size_written = (num_components + first_component) *
                           inst->dst.component_size(inst->exec_size);

      /* Copy the temporary to the destination to deal with writemasking.
       *
       * Also attempt to deal with gl_PointSize being in the .w component.
       */
      if (inst->offset == 0 && indirect_offset.file == BAD_FILE) {
         assert(type_sz(dst.type) == 4);
         inst->dst = bld.vgrf(dst.type, 4);
         inst->size_written = 4 * REG_SIZE;
         bld.MOV(dst, offset(inst->dst, bld, 3));
      }
      break;
   }

   case nir_intrinsic_load_output:
   case nir_intrinsic_load_per_vertex_output: {
      assert(nir_dest_bit_size(instr->dest) == 32);
      fs_reg indirect_offset = get_indirect_offset(instr);
      unsigned imm_offset = nir_intrinsic_base(instr);
      unsigned first_component = nir_intrinsic_component(instr);

      fs_inst *inst;
      if (indirect_offset.file == BAD_FILE) {
         /* This MOV replicates the output handle to all enabled channels
          * is SINGLE_PATCH mode.
          */
         fs_reg patch_handle = bld.vgrf(BRW_REGISTER_TYPE_UD, 1);
         bld.MOV(patch_handle, tcs_payload().patch_urb_output);

         {
            fs_reg srcs[URB_LOGICAL_NUM_SRCS];
            srcs[URB_LOGICAL_SRC_HANDLE] = patch_handle;

            if (first_component != 0) {
               unsigned read_components =
                  instr->num_components + first_component;
               fs_reg tmp = bld.vgrf(dst.type, read_components);
               inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, tmp,
                               srcs, ARRAY_SIZE(srcs));
               inst->size_written = read_components * REG_SIZE;
               for (unsigned i = 0; i < instr->num_components; i++) {
                  bld.MOV(offset(dst, bld, i),
                          offset(tmp, bld, i + first_component));
               }
            } else {
               inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, dst,
                               srcs, ARRAY_SIZE(srcs));
               inst->size_written = instr->num_components * REG_SIZE;
            }
            inst->offset = imm_offset;
            inst->mlen = 1;
         }
      } else {
         /* Indirect indexing - use per-slot offsets as well. */
         fs_reg srcs[URB_LOGICAL_NUM_SRCS];
         srcs[URB_LOGICAL_SRC_HANDLE] = tcs_payload().patch_urb_output;
         srcs[URB_LOGICAL_SRC_PER_SLOT_OFFSETS] = indirect_offset;

         if (first_component != 0) {
            unsigned read_components =
               instr->num_components + first_component;
            fs_reg tmp = bld.vgrf(dst.type, read_components);
            inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, tmp,
                            srcs, ARRAY_SIZE(srcs));
            inst->size_written = read_components * REG_SIZE;
            for (unsigned i = 0; i < instr->num_components; i++) {
               bld.MOV(offset(dst, bld, i),
                       offset(tmp, bld, i + first_component));
            }
         } else {
            inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, dst,
                            srcs, ARRAY_SIZE(srcs));
            inst->size_written = instr->num_components * REG_SIZE;
         }
         inst->offset = imm_offset;
         inst->mlen = 2;
      }
      break;
   }

   case nir_intrinsic_store_output:
   case nir_intrinsic_store_per_vertex_output: {
      assert(nir_src_bit_size(instr->src[0]) == 32);
      fs_reg value = get_nir_src(instr->src[0]);
      fs_reg indirect_offset = get_indirect_offset(instr);
      unsigned imm_offset = nir_intrinsic_base(instr);
      unsigned mask = nir_intrinsic_write_mask(instr);

      if (mask == 0)
         break;

      unsigned num_components = util_last_bit(mask);

      /* We can only pack two 64-bit components in a single message, so send
       * 2 messages if we have more components
       */
      unsigned first_component = nir_intrinsic_component(instr);
      mask = mask << first_component;

      fs_reg mask_reg;
      if (mask != WRITEMASK_XYZW)
         mask_reg = brw_imm_ud(mask << 16);

      fs_reg sources[4];

      for (unsigned i = 0; i < num_components; i++) {
         if (!(mask & (1 << (i + first_component))))
            continue;

         sources[i + first_component] = offset(value, bld, i);
      }

      unsigned header_size = 1 + unsigned(indirect_offset.file != BAD_FILE) +
         unsigned(mask != WRITEMASK_XYZW);
      const unsigned length = num_components + first_component;

      fs_reg srcs[URB_LOGICAL_NUM_SRCS];
      srcs[URB_LOGICAL_SRC_HANDLE] = tcs_payload().patch_urb_output;
      srcs[URB_LOGICAL_SRC_PER_SLOT_OFFSETS] = indirect_offset;
      srcs[URB_LOGICAL_SRC_CHANNEL_MASK] = mask_reg;
      srcs[URB_LOGICAL_SRC_DATA] = bld.vgrf(BRW_REGISTER_TYPE_F, length);
      bld.LOAD_PAYLOAD(srcs[URB_LOGICAL_SRC_DATA], sources, length, 0);

      fs_inst *inst = bld.emit(SHADER_OPCODE_URB_WRITE_LOGICAL, reg_undef,
                               srcs, ARRAY_SIZE(srcs));
      inst->offset = imm_offset;
      inst->mlen = header_size + length;
      break;
   }

   default:
      nir_emit_intrinsic(bld, instr);
      break;
   }
}

void
fs_visitor::nir_emit_tes_intrinsic(const fs_builder &bld,
                                   nir_intrinsic_instr *instr)
{
   assert(stage == MESA_SHADER_TESS_EVAL);
   struct brw_tes_prog_data *tes_prog_data = brw_tes_prog_data(prog_data);

   fs_reg dest;
   if (nir_intrinsic_infos[instr->intrinsic].has_dest)
      dest = get_nir_dest(instr->dest);

   switch (instr->intrinsic) {
   case nir_intrinsic_load_primitive_id:
      bld.MOV(dest, tes_payload().primitive_id);
      break;

   case nir_intrinsic_load_tess_coord:
      for (unsigned i = 0; i < 3; i++)
         bld.MOV(offset(dest, bld, i), tes_payload().coords[i]);
      break;

   case nir_intrinsic_load_input:
   case nir_intrinsic_load_per_vertex_input: {
      assert(nir_dest_bit_size(instr->dest) == 32);
      fs_reg indirect_offset = get_indirect_offset(instr);
      unsigned imm_offset = nir_intrinsic_base(instr);
      unsigned first_component = nir_intrinsic_component(instr);

      fs_inst *inst;
      if (indirect_offset.file == BAD_FILE) {
         /* Arbitrarily only push up to 32 vec4 slots worth of data,
          * which is 16 registers (since each holds 2 vec4 slots).
          */
         const unsigned max_push_slots = 32;
         if (imm_offset < max_push_slots) {
            fs_reg src = fs_reg(ATTR, imm_offset / 2, dest.type);
            for (int i = 0; i < instr->num_components; i++) {
               unsigned comp = 4 * (imm_offset % 2) + i + first_component;
               bld.MOV(offset(dest, bld, i), component(src, comp));
            }

            tes_prog_data->base.urb_read_length =
               MAX2(tes_prog_data->base.urb_read_length,
                    (imm_offset / 2) + 1);
         } else {
            /* Replicate the patch handle to all enabled channels */
            fs_reg srcs[URB_LOGICAL_NUM_SRCS];
            srcs[URB_LOGICAL_SRC_HANDLE] = tes_payload().patch_urb_input;

            if (first_component != 0) {
               unsigned read_components =
                  instr->num_components + first_component;
               fs_reg tmp = bld.vgrf(dest.type, read_components);
               inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, tmp,
                               srcs, ARRAY_SIZE(srcs));
               inst->size_written = read_components * REG_SIZE;
               for (unsigned i = 0; i < instr->num_components; i++) {
                  bld.MOV(offset(dest, bld, i),
                          offset(tmp, bld, i + first_component));
               }
            } else {
               inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, dest,
                               srcs, ARRAY_SIZE(srcs));
               inst->size_written = instr->num_components * REG_SIZE;
            }
            inst->mlen = 1;
            inst->offset = imm_offset;
         }
      } else {
         /* Indirect indexing - use per-slot offsets as well. */

         /* We can only read two double components with each URB read, so
          * we send two read messages in that case, each one loading up to
          * two double components.
          */
         unsigned num_components = instr->num_components;

         fs_reg srcs[URB_LOGICAL_NUM_SRCS];
         srcs[URB_LOGICAL_SRC_HANDLE] = tes_payload().patch_urb_input;
         srcs[URB_LOGICAL_SRC_PER_SLOT_OFFSETS] = indirect_offset;

         if (first_component != 0) {
            unsigned read_components =
                num_components + first_component;
            fs_reg tmp = bld.vgrf(dest.type, read_components);
            inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, tmp,
                            srcs, ARRAY_SIZE(srcs));
            for (unsigned i = 0; i < num_components; i++) {
               bld.MOV(offset(dest, bld, i),
                       offset(tmp, bld, i + first_component));
            }
         } else {
            inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, dest,
                            srcs, ARRAY_SIZE(srcs));
         }
         inst->mlen = 2;
         inst->offset = imm_offset;
         inst->size_written = (num_components + first_component) *
                              inst->dst.component_size(inst->exec_size);
      }
      break;
   }
   default:
      nir_emit_intrinsic(bld, instr);
      break;
   }
}

void
fs_visitor::nir_emit_gs_intrinsic(const fs_builder &bld,
                                  nir_intrinsic_instr *instr)
{
   assert(stage == MESA_SHADER_GEOMETRY);
   fs_reg indirect_offset;

   fs_reg dest;
   if (nir_intrinsic_infos[instr->intrinsic].has_dest)
      dest = get_nir_dest(instr->dest);

   switch (instr->intrinsic) {
   case nir_intrinsic_load_primitive_id:
      assert(stage == MESA_SHADER_GEOMETRY);
      assert(brw_gs_prog_data(prog_data)->include_primitive_id);
      bld.MOV(retype(dest, BRW_REGISTER_TYPE_UD), gs_payload().primitive_id);
      break;

   case nir_intrinsic_load_input:
      unreachable("load_input intrinsics are invalid for the GS stage");

   case nir_intrinsic_load_per_vertex_input:
      emit_gs_input_load(dest, instr->src[0], nir_intrinsic_base(instr),
                         instr->src[1], instr->num_components,
                         nir_intrinsic_component(instr));
      break;

   case nir_intrinsic_emit_vertex_with_counter:
      emit_gs_vertex(instr->src[0], nir_intrinsic_stream_id(instr));
      break;

   case nir_intrinsic_end_primitive_with_counter:
      emit_gs_end_primitive(instr->src[0]);
      break;

   case nir_intrinsic_set_vertex_and_primitive_count:
      bld.MOV(this->final_gs_vertex_count, get_nir_src(instr->src[0]));
      break;

   case nir_intrinsic_load_invocation_id: {
      fs_reg val = nir_system_values[SYSTEM_VALUE_INVOCATION_ID];
      assert(val.file != BAD_FILE);
      dest.type = val.type;
      bld.MOV(dest, val);
      break;
   }

   default:
      nir_emit_intrinsic(bld, instr);
      break;
   }
}

/**
 * Fetch the current render target layer index.
 */
static fs_reg
fetch_render_target_array_index(const fs_builder &bld)
{
   if (bld.shader->devinfo->ver >= 12) {
      /* The render target array index is provided in the thread payload as
       * bits 26:16 of r1.1.
       */
      const fs_reg idx = bld.vgrf(BRW_REGISTER_TYPE_UD);
      bld.AND(idx, brw_uw1_reg(BRW_GENERAL_REGISTER_FILE, 1, 3),
              brw_imm_uw(0x7ff));
      return idx;
   } else if (bld.shader->devinfo->ver >= 6) {
      /* The render target array index is provided in the thread payload as
       * bits 26:16 of r0.0.
       */
      const fs_reg idx = bld.vgrf(BRW_REGISTER_TYPE_UD);
      bld.AND(idx, brw_uw1_reg(BRW_GENERAL_REGISTER_FILE, 0, 1),
              brw_imm_uw(0x7ff));
      return idx;
   } else {
      /* Pre-SNB we only ever render into the first layer of the framebuffer
       * since layered rendering is not implemented.
       */
      return brw_imm_ud(0);
   }
}

/**
 * Fake non-coherent framebuffer read implemented using TXF to fetch from the
 * framebuffer at the current fragment coordinates and sample index.
 */
fs_inst *
fs_visitor::emit_non_coherent_fb_read(const fs_builder &bld, const fs_reg &dst,
                                      unsigned target)
{
   const struct intel_device_info *devinfo = bld.shader->devinfo;

   assert(bld.shader->stage == MESA_SHADER_FRAGMENT);
   const brw_wm_prog_key *wm_key =
      reinterpret_cast<const brw_wm_prog_key *>(key);
   assert(!wm_key->coherent_fb_fetch);

   /* Calculate the fragment coordinates. */
   const fs_reg coords = bld.vgrf(BRW_REGISTER_TYPE_UD, 3);
   bld.MOV(offset(coords, bld, 0), pixel_x);
   bld.MOV(offset(coords, bld, 1), pixel_y);
   bld.MOV(offset(coords, bld, 2), fetch_render_target_array_index(bld));

   /* Calculate the sample index and MCS payload when multisampling.  Luckily
    * the MCS fetch message behaves deterministically for UMS surfaces, so it
    * shouldn't be necessary to recompile based on whether the framebuffer is
    * CMS or UMS.
    */
   assert(wm_key->multisample_fbo == BRW_ALWAYS ||
          wm_key->multisample_fbo == BRW_NEVER);
   if (wm_key->multisample_fbo &&
       nir_system_values[SYSTEM_VALUE_SAMPLE_ID].file == BAD_FILE)
      nir_system_values[SYSTEM_VALUE_SAMPLE_ID] = emit_sampleid_setup();

   const fs_reg sample = nir_system_values[SYSTEM_VALUE_SAMPLE_ID];
   const fs_reg mcs = wm_key->multisample_fbo ?
      emit_mcs_fetch(coords, 3, brw_imm_ud(target), fs_reg()) : fs_reg();

   /* Use either a normal or a CMS texel fetch message depending on whether
    * the framebuffer is single or multisample.  On SKL+ use the wide CMS
    * message just in case the framebuffer uses 16x multisampling, it should
    * be equivalent to the normal CMS fetch for lower multisampling modes.
    */
   opcode op;
   if (wm_key->multisample_fbo) {
      /* On SKL+ use the wide CMS message just in case the framebuffer uses 16x
       * multisampling, it should be equivalent to the normal CMS fetch for
       * lower multisampling modes.
       *
       * On Gfx12HP, there is only CMS_W variant available.
       */
      if (devinfo->verx10 >= 125)
         op = SHADER_OPCODE_TXF_CMS_W_GFX12_LOGICAL;
      else if (devinfo->ver >= 9)
         op = SHADER_OPCODE_TXF_CMS_W_LOGICAL;
      else
         op = SHADER_OPCODE_TXF_CMS_LOGICAL;
   } else {
      op = SHADER_OPCODE_TXF_LOGICAL;
   }

   /* Emit the instruction. */
   fs_reg srcs[TEX_LOGICAL_NUM_SRCS];
   srcs[TEX_LOGICAL_SRC_COORDINATE]       = coords;
   srcs[TEX_LOGICAL_SRC_LOD]              = brw_imm_ud(0);
   srcs[TEX_LOGICAL_SRC_SAMPLE_INDEX]     = sample;
   srcs[TEX_LOGICAL_SRC_MCS]              = mcs;
   srcs[TEX_LOGICAL_SRC_SURFACE]          = brw_imm_ud(target);
   srcs[TEX_LOGICAL_SRC_SAMPLER]          = brw_imm_ud(0);
   srcs[TEX_LOGICAL_SRC_COORD_COMPONENTS] = brw_imm_ud(3);
   srcs[TEX_LOGICAL_SRC_GRAD_COMPONENTS]  = brw_imm_ud(0);

   fs_inst *inst = bld.emit(op, dst, srcs, ARRAY_SIZE(srcs));
   inst->size_written = 4 * inst->dst.component_size(inst->exec_size);

   return inst;
}

/**
 * Actual coherent framebuffer read implemented using the native render target
 * read message.  Requires SKL+.
 */
static fs_inst *
emit_coherent_fb_read(const fs_builder &bld, const fs_reg &dst, unsigned target)
{
   assert(bld.shader->devinfo->ver >= 9);
   fs_inst *inst = bld.emit(FS_OPCODE_FB_READ_LOGICAL, dst);
   inst->target = target;
   inst->size_written = 4 * inst->dst.component_size(inst->exec_size);

   return inst;
}

static fs_reg
alloc_temporary(const fs_builder &bld, unsigned size, fs_reg *regs, unsigned n)
{
   if (n && regs[0].file != BAD_FILE) {
      return regs[0];

   } else {
      const fs_reg tmp = bld.vgrf(BRW_REGISTER_TYPE_F, size);

      for (unsigned i = 0; i < n; i++)
         regs[i] = tmp;

      return tmp;
   }
}

static fs_reg
alloc_frag_output(fs_visitor *v, unsigned location)
{
   assert(v->stage == MESA_SHADER_FRAGMENT);
   const brw_wm_prog_key *const key =
      reinterpret_cast<const brw_wm_prog_key *>(v->key);
   const unsigned l = GET_FIELD(location, BRW_NIR_FRAG_OUTPUT_LOCATION);
   const unsigned i = GET_FIELD(location, BRW_NIR_FRAG_OUTPUT_INDEX);

   if (i > 0 || (key->force_dual_color_blend && l == FRAG_RESULT_DATA1))
      return alloc_temporary(v->bld, 4, &v->dual_src_output, 1);

   else if (l == FRAG_RESULT_COLOR)
      return alloc_temporary(v->bld, 4, v->outputs,
                             MAX2(key->nr_color_regions, 1));

   else if (l == FRAG_RESULT_DEPTH)
      return alloc_temporary(v->bld, 1, &v->frag_depth, 1);

   else if (l == FRAG_RESULT_STENCIL)
      return alloc_temporary(v->bld, 1, &v->frag_stencil, 1);

   else if (l == FRAG_RESULT_SAMPLE_MASK)
      return alloc_temporary(v->bld, 1, &v->sample_mask, 1);

   else if (l >= FRAG_RESULT_DATA0 &&
            l < FRAG_RESULT_DATA0 + BRW_MAX_DRAW_BUFFERS)
      return alloc_temporary(v->bld, 4,
                             &v->outputs[l - FRAG_RESULT_DATA0], 1);

   else
      unreachable("Invalid location");
}

void
fs_visitor::nir_emit_fs_intrinsic(const fs_builder &bld,
                                  nir_intrinsic_instr *instr)
{
   assert(stage == MESA_SHADER_FRAGMENT);

   fs_reg dest;
   if (nir_intrinsic_infos[instr->intrinsic].has_dest)
      dest = get_nir_dest(instr->dest);

   switch (instr->intrinsic) {
   case nir_intrinsic_load_front_face:
      bld.MOV(retype(dest, BRW_REGISTER_TYPE_D),
              emit_frontfacing_interpolation());
      break;

   case nir_intrinsic_load_sample_pos:
   case nir_intrinsic_load_sample_pos_or_center: {
      fs_reg sample_pos = nir_system_values[SYSTEM_VALUE_SAMPLE_POS];
      assert(sample_pos.file != BAD_FILE);
      dest.type = sample_pos.type;
      bld.MOV(dest, sample_pos);
      bld.MOV(offset(dest, bld, 1), offset(sample_pos, bld, 1));
      break;
   }

   case nir_intrinsic_load_layer_id:
      dest.type = BRW_REGISTER_TYPE_UD;
      bld.MOV(dest, fetch_render_target_array_index(bld));
      break;

   case nir_intrinsic_is_helper_invocation:
      emit_is_helper_invocation(dest);
      break;

   case nir_intrinsic_load_helper_invocation:
   case nir_intrinsic_load_sample_mask_in:
   case nir_intrinsic_load_sample_id:
   case nir_intrinsic_load_frag_shading_rate: {
      gl_system_value sv = nir_system_value_from_intrinsic(instr->intrinsic);
      fs_reg val = nir_system_values[sv];
      assert(val.file != BAD_FILE);
      dest.type = val.type;
      bld.MOV(dest, val);
      break;
   }

   case nir_intrinsic_store_output: {
      const fs_reg src = get_nir_src(instr->src[0]);
      const unsigned store_offset = nir_src_as_uint(instr->src[1]);
      const unsigned location = nir_intrinsic_base(instr) +
         SET_FIELD(store_offset, BRW_NIR_FRAG_OUTPUT_LOCATION);
      const fs_reg new_dest = retype(alloc_frag_output(this, location),
                                     src.type);

      for (unsigned j = 0; j < instr->num_components; j++)
         bld.MOV(offset(new_dest, bld, nir_intrinsic_component(instr) + j),
                 offset(src, bld, j));

      break;
   }

   case nir_intrinsic_load_output: {
      const unsigned l = GET_FIELD(nir_intrinsic_base(instr),
                                   BRW_NIR_FRAG_OUTPUT_LOCATION);
      assert(l >= FRAG_RESULT_DATA0);
      const unsigned load_offset = nir_src_as_uint(instr->src[0]);
      const unsigned target = l - FRAG_RESULT_DATA0 + load_offset;
      const fs_reg tmp = bld.vgrf(dest.type, 4);

      if (reinterpret_cast<const brw_wm_prog_key *>(key)->coherent_fb_fetch)
         emit_coherent_fb_read(bld, tmp, target);
      else
         emit_non_coherent_fb_read(bld, tmp, target);

      for (unsigned j = 0; j < instr->num_components; j++) {
         bld.MOV(offset(dest, bld, j),
                 offset(tmp, bld, nir_intrinsic_component(instr) + j));
      }

      break;
   }

   case nir_intrinsic_demote:
   case nir_intrinsic_discard:
   case nir_intrinsic_terminate:
   case nir_intrinsic_demote_if:
   case nir_intrinsic_discard_if:
   case nir_intrinsic_terminate_if: {
      /* We track our discarded pixels in f0.1/f1.0.  By predicating on it, we
       * can update just the flag bits that aren't yet discarded.  If there's
       * no condition, we emit a CMP of g0 != g0, so all currently executing
       * channels will get turned off.
       */
      fs_inst *cmp = NULL;
      if (instr->intrinsic == nir_intrinsic_demote_if ||
          instr->intrinsic == nir_intrinsic_discard_if ||
          instr->intrinsic == nir_intrinsic_terminate_if) {
         nir_alu_instr *alu = nir_src_as_alu_instr(instr->src[0]);

         if (alu != NULL &&
             alu->op != nir_op_bcsel &&
             (devinfo->ver > 5 ||
              (alu->instr.pass_flags & BRW_NIR_BOOLEAN_MASK) != BRW_NIR_BOOLEAN_NEEDS_RESOLVE ||
              alu->op == nir_op_fneu32 || alu->op == nir_op_feq32 ||
              alu->op == nir_op_flt32 || alu->op == nir_op_fge32 ||
              alu->op == nir_op_ine32 || alu->op == nir_op_ieq32 ||
              alu->op == nir_op_ilt32 || alu->op == nir_op_ige32 ||
              alu->op == nir_op_ult32 || alu->op == nir_op_uge32)) {
            /* Re-emit the instruction that generated the Boolean value, but
             * do not store it.  Since this instruction will be conditional,
             * other instructions that want to use the real Boolean value may
             * get garbage.  This was a problem for piglit's fs-discard-exit-2
             * test.
             *
             * Ideally we'd detect that the instruction cannot have a
             * conditional modifier before emitting the instructions.  Alas,
             * that is nigh impossible.  Instead, we're going to assume the
             * instruction (or last instruction) generated can have a
             * conditional modifier.  If it cannot, fallback to the old-style
             * compare, and hope dead code elimination will clean up the
             * extra instructions generated.
             */
            nir_emit_alu(bld, alu, false);

            cmp = (fs_inst *) instructions.get_tail();
            if (cmp->conditional_mod == BRW_CONDITIONAL_NONE) {
               if (cmp->can_do_cmod())
                  cmp->conditional_mod = BRW_CONDITIONAL_Z;
               else
                  cmp = NULL;
            } else {
               /* The old sequence that would have been generated is,
                * basically, bool_result == false.  This is equivalent to
                * !bool_result, so negate the old modifier.
                */
               cmp->conditional_mod = brw_negate_cmod(cmp->conditional_mod);
            }
         }

         if (cmp == NULL) {
            cmp = bld.CMP(bld.null_reg_f(), get_nir_src(instr->src[0]),
                          brw_imm_d(0), BRW_CONDITIONAL_Z);
         }
      } else {
         fs_reg some_reg = fs_reg(retype(brw_vec8_grf(0, 0),
                                       BRW_REGISTER_TYPE_UW));
         cmp = bld.CMP(bld.null_reg_f(), some_reg, some_reg, BRW_CONDITIONAL_NZ);
      }

      cmp->predicate = BRW_PREDICATE_NORMAL;
      cmp->flag_subreg = sample_mask_flag_subreg(this);

      fs_inst *jump = bld.emit(BRW_OPCODE_HALT);
      jump->flag_subreg = sample_mask_flag_subreg(this);
      jump->predicate_inverse = true;

      if (instr->intrinsic == nir_intrinsic_terminate ||
          instr->intrinsic == nir_intrinsic_terminate_if) {
         jump->predicate = BRW_PREDICATE_NORMAL;
      } else {
         /* Only jump when the whole quad is demoted.  For historical
          * reasons this is also used for discard.
          */
         jump->predicate = BRW_PREDICATE_ALIGN1_ANY4H;
      }

      if (devinfo->ver < 7)
         limit_dispatch_width(
            16, "Fragment discard/demote not implemented in SIMD32 mode.\n");
      break;
   }

   case nir_intrinsic_load_input: {
      /* In Fragment Shaders load_input is used either for flat inputs or
       * per-primitive inputs.
       */
      assert(nir_dest_bit_size(instr->dest) == 32);
      unsigned base = nir_intrinsic_base(instr);
      unsigned comp = nir_intrinsic_component(instr);
      unsigned num_components = instr->num_components;

      const struct brw_wm_prog_key *wm_key = (brw_wm_prog_key*) this->key;

      if (wm_key->mesh_input == BRW_SOMETIMES) {
         assert(devinfo->verx10 >= 125);
         /* The FS payload gives us the viewport and layer clamped to valid
          * ranges, but the spec for gl_ViewportIndex and gl_Layer includes
          * the language:
          *   the fragment stage will read the same value written by the
          *   geometry stage, even if that value is out of range.
          *
          * Which is why these are normally passed as regular attributes.
          * This isn't tested anywhere except some GL-only piglit tests
          * though, so for the case where the FS may be used against either a
          * traditional pipeline or a mesh one, where the position of these
          * will change depending on the previous stage, read them from the
          * payload to simplify things until the requisite magic is in place.
          */
         if (base == VARYING_SLOT_LAYER || base == VARYING_SLOT_VIEWPORT) {
            assert(num_components == 1);
            fs_reg g1(retype(brw_vec1_grf(1, 1), BRW_REGISTER_TYPE_UD));

            unsigned mask, shift_count;
            if (base == VARYING_SLOT_LAYER) {
               shift_count = 16;
               mask = 0x7ff << shift_count;
            } else {
               shift_count = 27;
               mask = 0xf << shift_count;
            }

            fs_reg vp_or_layer = bld.vgrf(BRW_REGISTER_TYPE_UD);
            bld.AND(vp_or_layer, g1, brw_imm_ud(mask));
            fs_reg shifted_value = bld.vgrf(BRW_REGISTER_TYPE_UD);
            bld.SHR(shifted_value, vp_or_layer, brw_imm_ud(shift_count));
            bld.MOV(offset(dest, bld, 0), retype(shifted_value, dest.type));
            break;
         }
      }

      /* TODO(mesh): Multiview. Verify and handle these special cases for Mesh. */

      /* Special case fields in the VUE header */
      if (base == VARYING_SLOT_LAYER)
         comp = 1;
      else if (base == VARYING_SLOT_VIEWPORT)
         comp = 2;

      if (BITFIELD64_BIT(base) & nir->info.per_primitive_inputs) {
         assert(base != VARYING_SLOT_PRIMITIVE_INDICES);
         for (unsigned int i = 0; i < num_components; i++) {
            bld.MOV(offset(dest, bld, i),
                    retype(per_primitive_reg(base, comp + i), dest.type));
         }
      } else {
         for (unsigned int i = 0; i < num_components; i++) {
            bld.MOV(offset(dest, bld, i),
                    retype(component(interp_reg(base, comp + i), 3), dest.type));
         }
      }
      break;
   }

   case nir_intrinsic_load_fs_input_interp_deltas: {
      assert(stage == MESA_SHADER_FRAGMENT);
      assert(nir_src_as_uint(instr->src[0]) == 0);
      fs_reg interp = interp_reg(nir_intrinsic_base(instr),
                                 nir_intrinsic_component(instr));
      dest.type = BRW_REGISTER_TYPE_F;
      bld.MOV(offset(dest, bld, 0), component(interp, 3));
      bld.MOV(offset(dest, bld, 1), component(interp, 1));
      bld.MOV(offset(dest, bld, 2), component(interp, 0));
      break;
   }

   case nir_intrinsic_load_barycentric_pixel:
   case nir_intrinsic_load_barycentric_centroid:
   case nir_intrinsic_load_barycentric_sample: {
      /* Use the delta_xy values computed from the payload */
      enum brw_barycentric_mode bary = brw_barycentric_mode(instr);
      const fs_reg srcs[] = { offset(this->delta_xy[bary], bld, 0),
                              offset(this->delta_xy[bary], bld, 1) };
      bld.LOAD_PAYLOAD(dest, srcs, ARRAY_SIZE(srcs), 0);
      break;
   }

   case nir_intrinsic_load_barycentric_at_sample: {
      const glsl_interp_mode interpolation =
         (enum glsl_interp_mode) nir_intrinsic_interp_mode(instr);

      if (nir_src_is_const(instr->src[0])) {
         unsigned msg_data = nir_src_as_uint(instr->src[0]) << 4;

         emit_pixel_interpolater_send(bld,
                                      FS_OPCODE_INTERPOLATE_AT_SAMPLE,
                                      dest,
                                      fs_reg(), /* src */
                                      brw_imm_ud(msg_data),
                                      interpolation);
      } else {
         const fs_reg sample_src = retype(get_nir_src(instr->src[0]),
                                          BRW_REGISTER_TYPE_UD);

         if (nir_src_is_always_uniform(instr->src[0])) {
            const fs_reg sample_id = bld.emit_uniformize(sample_src);
            const fs_reg msg_data = vgrf(glsl_type::uint_type);
            bld.exec_all().group(1, 0)
               .SHL(msg_data, sample_id, brw_imm_ud(4u));
            emit_pixel_interpolater_send(bld,
                                         FS_OPCODE_INTERPOLATE_AT_SAMPLE,
                                         dest,
                                         fs_reg(), /* src */
                                         component(msg_data, 0),
                                         interpolation);
         } else {
            /* Make a loop that sends a message to the pixel interpolater
             * for the sample number in each live channel. If there are
             * multiple channels with the same sample number then these
             * will be handled simultaneously with a single iteration of
             * the loop.
             */
            bld.emit(BRW_OPCODE_DO);

            /* Get the next live sample number into sample_id_reg */
            const fs_reg sample_id = bld.emit_uniformize(sample_src);

            /* Set the flag register so that we can perform the send
             * message on all channels that have the same sample number
             */
            bld.CMP(bld.null_reg_ud(),
                    sample_src, sample_id,
                    BRW_CONDITIONAL_EQ);
            const fs_reg msg_data = vgrf(glsl_type::uint_type);
            bld.exec_all().group(1, 0)
               .SHL(msg_data, sample_id, brw_imm_ud(4u));
            fs_inst *inst =
               emit_pixel_interpolater_send(bld,
                                            FS_OPCODE_INTERPOLATE_AT_SAMPLE,
                                            dest,
                                            fs_reg(), /* src */
                                            component(msg_data, 0),
                                            interpolation);
            set_predicate(BRW_PREDICATE_NORMAL, inst);

            /* Continue the loop if there are any live channels left */
            set_predicate_inv(BRW_PREDICATE_NORMAL,
                              true, /* inverse */
                              bld.emit(BRW_OPCODE_WHILE));
         }
      }
      break;
   }

   case nir_intrinsic_load_barycentric_at_offset: {
      const glsl_interp_mode interpolation =
         (enum glsl_interp_mode) nir_intrinsic_interp_mode(instr);

      nir_const_value *const_offset = nir_src_as_const_value(instr->src[0]);

      if (const_offset) {
         assert(nir_src_bit_size(instr->src[0]) == 32);
         unsigned off_x = const_offset[0].u32 & 0xf;
         unsigned off_y = const_offset[1].u32 & 0xf;

         emit_pixel_interpolater_send(bld,
                                      FS_OPCODE_INTERPOLATE_AT_SHARED_OFFSET,
                                      dest,
                                      fs_reg(), /* src */
                                      brw_imm_ud(off_x | (off_y << 4)),
                                      interpolation);
      } else {
         fs_reg src = retype(get_nir_src(instr->src[0]), BRW_REGISTER_TYPE_D);
         const enum opcode opcode = FS_OPCODE_INTERPOLATE_AT_PER_SLOT_OFFSET;
         emit_pixel_interpolater_send(bld,
                                      opcode,
                                      dest,
                                      src,
                                      brw_imm_ud(0u),
                                      interpolation);
      }
      break;
   }

   case nir_intrinsic_load_frag_coord:
      emit_fragcoord_interpolation(dest);
      break;

   case nir_intrinsic_load_interpolated_input: {
      assert(instr->src[0].ssa &&
             instr->src[0].ssa->parent_instr->type == nir_instr_type_intrinsic);
      nir_intrinsic_instr *bary_intrinsic =
         nir_instr_as_intrinsic(instr->src[0].ssa->parent_instr);
      nir_intrinsic_op bary_intrin = bary_intrinsic->intrinsic;
      enum glsl_interp_mode interp_mode =
         (enum glsl_interp_mode) nir_intrinsic_interp_mode(bary_intrinsic);
      fs_reg dst_xy;

      if (bary_intrin == nir_intrinsic_load_barycentric_at_offset ||
          bary_intrin == nir_intrinsic_load_barycentric_at_sample) {
         /* Use the result of the PI message. */
         dst_xy = retype(get_nir_src(instr->src[0]), BRW_REGISTER_TYPE_F);
      } else {
         /* Use the delta_xy values computed from the payload */
         enum brw_barycentric_mode bary = brw_barycentric_mode(bary_intrinsic);
         dst_xy = this->delta_xy[bary];
      }

      for (unsigned int i = 0; i < instr->num_components; i++) {
         fs_reg interp =
            component(interp_reg(nir_intrinsic_base(instr),
                                 nir_intrinsic_component(instr) + i), 0);
         interp.type = BRW_REGISTER_TYPE_F;
         dest.type = BRW_REGISTER_TYPE_F;

         if (devinfo->ver < 6 && interp_mode == INTERP_MODE_SMOOTH) {
            fs_reg tmp = vgrf(glsl_type::float_type);
            bld.emit(FS_OPCODE_LINTERP, tmp, dst_xy, interp);
            bld.MUL(offset(dest, bld, i), tmp, this->pixel_w);
         } else {
            bld.emit(FS_OPCODE_LINTERP, offset(dest, bld, i), dst_xy, interp);
         }
      }
      break;
   }

   default:
      nir_emit_intrinsic(bld, instr);
      break;
   }
}

void
fs_visitor::nir_emit_cs_intrinsic(const fs_builder &bld,
                                  nir_intrinsic_instr *instr)
{
   assert(gl_shader_stage_uses_workgroup(stage));
   struct brw_cs_prog_data *cs_prog_data = brw_cs_prog_data(prog_data);

   fs_reg dest;
   if (nir_intrinsic_infos[instr->intrinsic].has_dest)
      dest = get_nir_dest(instr->dest);

   switch (instr->intrinsic) {
   case nir_intrinsic_scoped_barrier:
      if (nir_intrinsic_memory_scope(instr) != SCOPE_NONE)
         nir_emit_intrinsic(bld, instr);
      if (nir_intrinsic_execution_scope(instr) == SCOPE_WORKGROUP) {
         /* The whole workgroup fits in a single HW thread, so all the
          * invocations are already executed lock-step.  Instead of an actual
          * barrier just emit a scheduling fence, that will generate no code.
          */
         if (!nir->info.workgroup_size_variable &&
             workgroup_size() <= dispatch_width) {
            bld.exec_all().group(1, 0).emit(FS_OPCODE_SCHEDULING_FENCE);
            break;
         }

         emit_barrier();
         cs_prog_data->uses_barrier = true;
      }
      break;

   case nir_intrinsic_load_subgroup_id:
      cs_payload().load_subgroup_id(bld, dest);
      break;

   case nir_intrinsic_load_local_invocation_id: {
      fs_reg val = nir_system_values[SYSTEM_VALUE_LOCAL_INVOCATION_ID];
      assert(val.file != BAD_FILE);
      dest.type = val.type;
      for (unsigned i = 0; i < 3; i++)
         bld.MOV(offset(dest, bld, i), offset(val, bld, i));
      break;
   }

   case nir_intrinsic_load_workgroup_id:
   case nir_intrinsic_load_workgroup_id_zero_base: {
      fs_reg val = nir_system_values[SYSTEM_VALUE_WORKGROUP_ID];
      assert(val.file != BAD_FILE);
      dest.type = val.type;
      for (unsigned i = 0; i < 3; i++)
         bld.MOV(offset(dest, bld, i), offset(val, bld, i));
      break;
   }

   case nir_intrinsic_load_num_workgroups: {
      assert(nir_dest_bit_size(instr->dest) == 32);

      cs_prog_data->uses_num_work_groups = true;

      fs_reg srcs[SURFACE_LOGICAL_NUM_SRCS];
      srcs[SURFACE_LOGICAL_SRC_SURFACE] = brw_imm_ud(0);
      srcs[SURFACE_LOGICAL_SRC_IMM_DIMS] = brw_imm_ud(1);
      srcs[SURFACE_LOGICAL_SRC_IMM_ARG] = brw_imm_ud(3); /* num components */
      srcs[SURFACE_LOGICAL_SRC_ADDRESS] = brw_imm_ud(0);
      srcs[SURFACE_LOGICAL_SRC_ALLOW_SAMPLE_MASK] = brw_imm_ud(0);
      fs_inst *inst =
         bld.emit(SHADER_OPCODE_UNTYPED_SURFACE_READ_LOGICAL,
                  dest, srcs, SURFACE_LOGICAL_NUM_SRCS);
      inst->size_written = 3 * dispatch_width * 4;
      break;
   }

   case nir_intrinsic_shared_atomic:
   case nir_intrinsic_shared_atomic_swap:
      nir_emit_surface_atomic(bld, instr, brw_imm_ud(GFX7_BTI_SLM),
                              false /* bindless */);
      break;

   case nir_intrinsic_load_shared: {
      assert(devinfo->ver >= 7);

      const unsigned bit_size = nir_dest_bit_size(instr->dest);
      fs_reg srcs[SURFACE_LOGICAL_NUM_SRCS];
      srcs[SURFACE_LOGICAL_SRC_SURFACE] = brw_imm_ud(GFX7_BTI_SLM);

      fs_reg addr = get_nir_src(instr->src[0]);
      int base = nir_intrinsic_base(instr);
      if (base) {
         fs_reg addr_off = bld.vgrf(BRW_REGISTER_TYPE_UD, 1);
         bld.ADD(addr_off, addr, brw_imm_d(base));
         srcs[SURFACE_LOGICAL_SRC_ADDRESS] = addr_off;
      } else {
         srcs[SURFACE_LOGICAL_SRC_ADDRESS] = addr;
      }

      srcs[SURFACE_LOGICAL_SRC_IMM_DIMS] = brw_imm_ud(1);
      srcs[SURFACE_LOGICAL_SRC_ALLOW_SAMPLE_MASK] = brw_imm_ud(0);

      /* Make dest unsigned because that's what the temporary will be */
      dest.type = brw_reg_type_from_bit_size(bit_size, BRW_REGISTER_TYPE_UD);

      /* Read the vector */
      assert(bit_size <= 32);
      assert(nir_intrinsic_align(instr) > 0);
      if (bit_size == 32 &&
          nir_intrinsic_align(instr) >= 4) {
         assert(nir_dest_num_components(instr->dest) <= 4);
         srcs[SURFACE_LOGICAL_SRC_IMM_ARG] = brw_imm_ud(instr->num_components);
         fs_inst *inst =
            bld.emit(SHADER_OPCODE_UNTYPED_SURFACE_READ_LOGICAL,
                     dest, srcs, SURFACE_LOGICAL_NUM_SRCS);
         inst->size_written = instr->num_components * dispatch_width * 4;
      } else {
         assert(nir_dest_num_components(instr->dest) == 1);
         srcs[SURFACE_LOGICAL_SRC_IMM_ARG] = brw_imm_ud(bit_size);

         fs_reg read_result = bld.vgrf(BRW_REGISTER_TYPE_UD);
         bld.emit(SHADER_OPCODE_BYTE_SCATTERED_READ_LOGICAL,
                  read_result, srcs, SURFACE_LOGICAL_NUM_SRCS);
         bld.MOV(dest, subscript(read_result, dest.type, 0));
      }
      break;
   }

   case nir_intrinsic_store_shared: {
      assert(devinfo->ver >= 7);

      const unsigned bit_size = nir_src_bit_size(instr->src[0]);
      fs_reg srcs[SURFACE_LOGICAL_NUM_SRCS];
      srcs[SURFACE_LOGICAL_SRC_SURFACE] = brw_imm_ud(GFX7_BTI_SLM);

      fs_reg addr = get_nir_src(instr->src[1]);
      int base = nir_intrinsic_base(instr);
      if (base) {
         fs_reg addr_off = bld.vgrf(BRW_REGISTER_TYPE_UD, 1);
         bld.ADD(addr_off, addr, brw_imm_d(base));
         srcs[SURFACE_LOGICAL_SRC_ADDRESS] = addr_off;
      } else {
         srcs[SURFACE_LOGICAL_SRC_ADDRESS] = addr;
      }

      srcs[SURFACE_LOGICAL_SRC_IMM_DIMS] = brw_imm_ud(1);
      /* No point in masking with sample mask, here we're handling compute
       * intrinsics.
       */
      srcs[SURFACE_LOGICAL_SRC_ALLOW_SAMPLE_MASK] = brw_imm_ud(0);

      fs_reg data = get_nir_src(instr->src[0]);
      data.type = brw_reg_type_from_bit_size(bit_size, BRW_REGISTER_TYPE_UD);

      assert(bit_size <= 32);
      assert(nir_intrinsic_write_mask(instr) ==
             (1u << instr->num_components) - 1);
      assert(nir_intrinsic_align(instr) > 0);
      if (bit_size == 32 &&
          nir_intrinsic_align(instr) >= 4) {
         assert(nir_src_num_components(instr->src[0]) <= 4);
         srcs[SURFACE_LOGICAL_SRC_DATA] = data;
         srcs[SURFACE_LOGICAL_SRC_IMM_ARG] = brw_imm_ud(instr->num_components);
         bld.emit(SHADER_OPCODE_UNTYPED_SURFACE_WRITE_LOGICAL,
                  fs_reg(), srcs, SURFACE_LOGICAL_NUM_SRCS);
      } else {
         assert(nir_src_num_components(instr->src[0]) == 1);
         srcs[SURFACE_LOGICAL_SRC_IMM_ARG] = brw_imm_ud(bit_size);

         srcs[SURFACE_LOGICAL_SRC_DATA] = bld.vgrf(BRW_REGISTER_TYPE_UD);
         bld.MOV(srcs[SURFACE_LOGICAL_SRC_DATA], data);

         bld.emit(SHADER_OPCODE_BYTE_SCATTERED_WRITE_LOGICAL,
                  fs_reg(), srcs, SURFACE_LOGICAL_NUM_SRCS);
      }
      break;
   }

   case nir_intrinsic_load_workgroup_size: {
      /* Should have been lowered by brw_nir_lower_cs_intrinsics() or
       * crocus/iris_setup_uniforms() for the variable group size case.
       */
      unreachable("Should have been lowered");
      break;
   }

   default:
      nir_emit_intrinsic(bld, instr);
      break;
   }
}

static void
emit_rt_lsc_fence(const fs_builder &bld,
                  enum lsc_fence_scope scope,
                  enum lsc_flush_type flush_type)
{
   const intel_device_info *devinfo = bld.shader->devinfo;

   const fs_builder ubld = bld.exec_all().group(8, 0);
   fs_reg tmp = ubld.vgrf(BRW_REGISTER_TYPE_UD);
   fs_inst *send = ubld.emit(SHADER_OPCODE_SEND, tmp,
                             brw_imm_ud(0) /* desc */,
                             brw_imm_ud(0) /* ex_desc */,
                             brw_vec8_grf(0, 0) /* payload */);
   send->sfid = GFX12_SFID_UGM;
   send->desc = lsc_fence_msg_desc(devinfo, scope, flush_type, true);
   send->mlen = 1; /* g0 header */
   send->ex_mlen = 0;
   send->size_written = REG_SIZE; /* Temp write for scheduling */
   send->send_has_side_effects = true;

   ubld.emit(FS_OPCODE_SCHEDULING_FENCE, ubld.null_reg_ud(), tmp);
}


void
fs_visitor::nir_emit_bs_intrinsic(const fs_builder &bld,
                                  nir_intrinsic_instr *instr)
{
   assert(brw_shader_stage_is_bindless(stage));
   const bs_thread_payload &payload = bs_payload();

   fs_reg dest;
   if (nir_intrinsic_infos[instr->intrinsic].has_dest)
      dest = get_nir_dest(instr->dest);

   switch (instr->intrinsic) {
   case nir_intrinsic_load_btd_global_arg_addr_intel:
      bld.MOV(dest, retype(payload.global_arg_ptr, dest.type));
      break;

   case nir_intrinsic_load_btd_local_arg_addr_intel:
      bld.MOV(dest, retype(payload.local_arg_ptr, dest.type));
      break;

   case nir_intrinsic_load_btd_shader_type_intel:
      payload.load_shader_type(bld, dest);
      break;

   default:
      nir_emit_intrinsic(bld, instr);
      break;
   }
}

static fs_reg
brw_nir_reduction_op_identity(const fs_builder &bld,
                              nir_op op, brw_reg_type type)
{
   nir_const_value value = nir_alu_binop_identity(op, type_sz(type) * 8);
   switch (type_sz(type)) {
   case 1:
      if (type == BRW_REGISTER_TYPE_UB) {
         return brw_imm_uw(value.u8);
      } else {
         assert(type == BRW_REGISTER_TYPE_B);
         return brw_imm_w(value.i8);
      }
   case 2:
      return retype(brw_imm_uw(value.u16), type);
   case 4:
      return retype(brw_imm_ud(value.u32), type);
   case 8:
      if (type == BRW_REGISTER_TYPE_DF)
         return setup_imm_df(bld, value.f64);
      else
         return retype(brw_imm_u64(value.u64), type);
   default:
      unreachable("Invalid type size");
   }
}

static opcode
brw_op_for_nir_reduction_op(nir_op op)
{
   switch (op) {
   case nir_op_iadd: return BRW_OPCODE_ADD;
   case nir_op_fadd: return BRW_OPCODE_ADD;
   case nir_op_imul: return BRW_OPCODE_MUL;
   case nir_op_fmul: return BRW_OPCODE_MUL;
   case nir_op_imin: return BRW_OPCODE_SEL;
   case nir_op_umin: return BRW_OPCODE_SEL;
   case nir_op_fmin: return BRW_OPCODE_SEL;
   case nir_op_imax: return BRW_OPCODE_SEL;
   case nir_op_umax: return BRW_OPCODE_SEL;
   case nir_op_fmax: return BRW_OPCODE_SEL;
   case nir_op_iand: return BRW_OPCODE_AND;
   case nir_op_ior:  return BRW_OPCODE_OR;
   case nir_op_ixor: return BRW_OPCODE_XOR;
   default:
      unreachable("Invalid reduction operation");
   }
}

static brw_conditional_mod
brw_cond_mod_for_nir_reduction_op(nir_op op)
{
   switch (op) {
   case nir_op_iadd: return BRW_CONDITIONAL_NONE;
   case nir_op_fadd: return BRW_CONDITIONAL_NONE;
   case nir_op_imul: return BRW_CONDITIONAL_NONE;
   case nir_op_fmul: return BRW_CONDITIONAL_NONE;
   case nir_op_imin: return BRW_CONDITIONAL_L;
   case nir_op_umin: return BRW_CONDITIONAL_L;
   case nir_op_fmin: return BRW_CONDITIONAL_L;
   case nir_op_imax: return BRW_CONDITIONAL_GE;
   case nir_op_umax: return BRW_CONDITIONAL_GE;
   case nir_op_fmax: return BRW_CONDITIONAL_GE;
   case nir_op_iand: return BRW_CONDITIONAL_NONE;
   case nir_op_ior:  return BRW_CONDITIONAL_NONE;
   case nir_op_ixor: return BRW_CONDITIONAL_NONE;
   default:
      unreachable("Invalid reduction operation");
   }
}

struct rebuild_resource {
   unsigned idx;
   std::vector<nir_ssa_def *> array;
};

static bool
add_rebuild_src(nir_src *src, void *state)
{
   struct rebuild_resource *res = (struct rebuild_resource *) state;

   if (!src->is_ssa)
      return false;

   for (nir_ssa_def *def : res->array) {
      if (def == src->ssa)
         return true;
   }

   nir_foreach_src(src->ssa->parent_instr, add_rebuild_src, state);
   res->array.push_back(src->ssa);
   return true;
}

fs_reg
fs_visitor::try_rebuild_resource(const brw::fs_builder &bld, nir_ssa_def *resource_def)
{
   /* Create a build at the location of the resource_intel intrinsic */
   fs_builder ubld1 = bld.exec_all().group(1, 0);

   struct rebuild_resource resources = {};
   resources.idx = 0;

   if (!nir_foreach_src(resource_def->parent_instr,
                        add_rebuild_src, &resources))
      return fs_reg();
   resources.array.push_back(resource_def);

   if (resources.array.size() == 1) {
      nir_ssa_def *def = resources.array[0];

      if (def->parent_instr->type == nir_instr_type_load_const) {
         nir_load_const_instr *load_const =
            nir_instr_as_load_const(def->parent_instr);
         return brw_imm_ud(load_const->value[0].i32);
      } else {
         assert(def->parent_instr->type == nir_instr_type_intrinsic &&
                (nir_instr_as_intrinsic(def->parent_instr)->intrinsic ==
                 nir_intrinsic_load_uniform));
         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(def->parent_instr);
         unsigned base_offset = nir_intrinsic_base(intrin);
         unsigned load_offset = nir_src_as_uint(intrin->src[0]);
         fs_reg src(UNIFORM, base_offset / 4, BRW_REGISTER_TYPE_UD);
         src.offset = load_offset + base_offset % 4;
         return src;
      }
   }

   for (unsigned i = 0; i < resources.array.size(); i++) {
      nir_ssa_def *def = resources.array[i];

      nir_instr *instr = def->parent_instr;
      switch (instr->type) {
      case nir_instr_type_load_const: {
         nir_load_const_instr *load_const =
            nir_instr_as_load_const(instr);
         fs_reg dst = ubld1.vgrf(BRW_REGISTER_TYPE_UD);
         ubld1.UNDEF(dst);
         nir_resource_insts[def->index] =
            ubld1.group(8, 0).MOV(dst, brw_imm_ud(load_const->value[0].i32));
         break;
      }

      case nir_instr_type_alu: {
         nir_alu_instr *alu = nir_instr_as_alu(instr);

         if (nir_op_infos[alu->op].num_inputs != 2)
            break;

         if (!alu->src[0].src.is_ssa ||
             !alu->src[1].src.is_ssa ||
             alu->src[0].negate ||
             alu->src[0].abs ||
             alu->src[0].swizzle[0] != 0 ||
             alu->src[1].negate ||
             alu->src[1].abs ||
             alu->src[1].swizzle[0] != 0)
            break;

         switch (alu->op) {
         case nir_op_iadd: {
            fs_reg dst = ubld1.vgrf(BRW_REGISTER_TYPE_UD);
            ubld1.UNDEF(dst);
            fs_reg src0 = nir_resource_insts[alu->src[0].src.ssa->index]->dst;
            fs_reg src1 = nir_resource_insts[alu->src[1].src.ssa->index]->dst;
            assert(src0.file != BAD_FILE && src1.file != BAD_FILE);
            assert(src0.type == BRW_REGISTER_TYPE_UD);
            nir_resource_insts[def->index] =
               ubld1.ADD(dst,
                         src0.file != IMM ? src0 : src1,
                         src0.file != IMM ? src1 : src0);
            break;
         }
         case nir_op_ushr: {
            assert(ubld1.dispatch_width() == 1);
            fs_reg dst = ubld1.vgrf(BRW_REGISTER_TYPE_UD);
            ubld1.UNDEF(dst);
            fs_reg src0 = nir_resource_insts[alu->src[0].src.ssa->index]->dst;
            fs_reg src1 = nir_resource_insts[alu->src[1].src.ssa->index]->dst;
            assert(src0.file != BAD_FILE && src1.file != BAD_FILE);
            assert(src0.type == BRW_REGISTER_TYPE_UD);
            nir_resource_insts[def->index] = ubld1.SHR(dst, src0, src1);
            break;
         }
         case nir_op_mov: {
            break;
         }
         default:
            break;
         }
         break;
      }

      case nir_instr_type_intrinsic: {
         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         switch (intrin->intrinsic) {
         case nir_intrinsic_resource_intel:
            nir_resource_insts[def->index] =
               nir_resource_insts[intrin->src[1].ssa->index];
            break;

         case nir_intrinsic_load_uniform: {
            if (!nir_src_is_const(intrin->src[0]))
               break;

            unsigned base_offset = nir_intrinsic_base(intrin);
            unsigned load_offset = nir_src_as_uint(intrin->src[0]);
            fs_reg dst = ubld1.vgrf(BRW_REGISTER_TYPE_UD);
            ubld1.UNDEF(dst);
            fs_reg src(UNIFORM, base_offset / 4, BRW_REGISTER_TYPE_UD);
            src.offset = load_offset + base_offset % 4;
            nir_resource_insts[def->index] = ubld1.MOV(dst, src);
            break;
         }

         default:
            break;
         }
         break;
      }

      default:
         break;
      }

      if (nir_resource_insts[def->index] == NULL)
         return fs_reg();
   }

   assert(nir_resource_insts[resource_def->index] != NULL);
   return component(nir_resource_insts[resource_def->index]->dst, 0);
}

fs_reg
fs_visitor::get_nir_image_intrinsic_image(const brw::fs_builder &bld,
                                          nir_intrinsic_instr *instr)
{
   if (is_resource_src(instr->src[0])) {
      fs_reg surf_index = get_resource_nir_src(instr->src[0]);
      if (surf_index.file != BAD_FILE)
         return surf_index;
   }

   fs_reg image = retype(get_nir_src_imm(instr->src[0]), BRW_REGISTER_TYPE_UD);
   fs_reg surf_index = image;

   return bld.emit_uniformize(surf_index);
}

fs_reg
fs_visitor::get_nir_buffer_intrinsic_index(const brw::fs_builder &bld,
                                           nir_intrinsic_instr *instr)
{
   /* SSBO stores are weird in that their index is in src[1] */
   const bool is_store =
      instr->intrinsic == nir_intrinsic_store_ssbo ||
      instr->intrinsic == nir_intrinsic_store_ssbo_block_intel;
   nir_src src = is_store ? instr->src[1] : instr->src[0];

   if (nir_src_is_const(src)) {
      return brw_imm_ud(nir_src_as_uint(src));
   } else if (is_resource_src(src)) {
      fs_reg surf_index = get_resource_nir_src(src);
      if (surf_index.file != BAD_FILE)
         return surf_index;
   }
   return bld.emit_uniformize(get_nir_src(src));
}

/**
 * The offsets we get from NIR act as if each SIMD channel has it's own blob
 * of contiguous space.  However, if we actually place each SIMD channel in
 * it's own space, we end up with terrible cache performance because each SIMD
 * channel accesses a different cache line even when they're all accessing the
 * same byte offset.  To deal with this problem, we swizzle the address using
 * a simple algorithm which ensures that any time a SIMD message reads or
 * writes the same address, it's all in the same cache line.  We have to keep
 * the bottom two bits fixed so that we can read/write up to a dword at a time
 * and the individual element is contiguous.  We do this by splitting the
 * address as follows:
 *
 *    31                             4-6           2          0
 *    +-------------------------------+------------+----------+
 *    |        Hi address bits        | chan index | addr low |
 *    +-------------------------------+------------+----------+
 *
 * In other words, the bottom two address bits stay, and the top 30 get
 * shifted up so that we can stick the SIMD channel index in the middle.  This
 * way, we can access 8, 16, or 32-bit elements and, when accessing a 32-bit
 * at the same logical offset, the scratch read/write instruction acts on
 * continuous elements and we get good cache locality.
 */
fs_reg
fs_visitor::swizzle_nir_scratch_addr(const brw::fs_builder &bld,
                                     const fs_reg &nir_addr,
                                     bool in_dwords)
{
   const fs_reg &chan_index =
      nir_system_values[SYSTEM_VALUE_SUBGROUP_INVOCATION];
   const unsigned chan_index_bits = ffs(dispatch_width) - 1;

   fs_reg addr = bld.vgrf(BRW_REGISTER_TYPE_UD);
   if (in_dwords) {
      /* In this case, we know the address is aligned to a DWORD and we want
       * the final address in DWORDs.
       */
      bld.SHL(addr, nir_addr, brw_imm_ud(chan_index_bits - 2));
      bld.OR(addr, addr, chan_index);
   } else {
      /* This case substantially more annoying because we have to pay
       * attention to those pesky two bottom bits.
       */
      fs_reg addr_hi = bld.vgrf(BRW_REGISTER_TYPE_UD);
      bld.AND(addr_hi, nir_addr, brw_imm_ud(~0x3u));
      bld.SHL(addr_hi, addr_hi, brw_imm_ud(chan_index_bits));
      fs_reg chan_addr = bld.vgrf(BRW_REGISTER_TYPE_UD);
      bld.SHL(chan_addr, chan_index, brw_imm_ud(2));
      bld.AND(addr, nir_addr, brw_imm_ud(0x3u));
      bld.OR(addr, addr, addr_hi);
      bld.OR(addr, addr, chan_addr);
   }
   return addr;
}

static unsigned
choose_oword_block_size_dwords(const struct intel_device_info *devinfo,
                               unsigned dwords)
{
   unsigned block;
   if (devinfo->has_lsc && dwords >= 64) {
      block = 64;
   } else if (dwords >= 32) {
      block = 32;
   } else if (dwords >= 16) {
      block = 16;
   } else {
      block = 8;
   }
   assert(block <= dwords);
   return block;
}

static void
increment_a64_address(const fs_builder &bld, fs_reg address, uint32_t v)
{
   if (bld.shader->devinfo->has_64bit_int) {
      bld.ADD(address, address, brw_imm_ud(v));
   } else {
      fs_reg low = retype(address, BRW_REGISTER_TYPE_UD);
      fs_reg high = offset(low, bld, 1);

      /* Add low and if that overflows, add carry to high. */
      bld.ADD(low, low, brw_imm_ud(v))->conditional_mod = BRW_CONDITIONAL_O;
      bld.ADD(high, high, brw_imm_ud(0x1))->predicate = BRW_PREDICATE_NORMAL;
   }
}

static fs_reg
emit_fence(const fs_builder &bld, enum opcode opcode,
           uint8_t sfid, uint32_t desc,
           bool commit_enable, uint8_t bti)
{
   assert(opcode == SHADER_OPCODE_INTERLOCK ||
          opcode == SHADER_OPCODE_MEMORY_FENCE);

   fs_reg dst = bld.vgrf(BRW_REGISTER_TYPE_UD);
   fs_inst *fence = bld.emit(opcode, dst, brw_vec8_grf(0, 0),
                             brw_imm_ud(commit_enable),
                             brw_imm_ud(bti));
   fence->sfid = sfid;
   fence->desc = desc;

   return dst;
}

static uint32_t
lsc_fence_descriptor_for_intrinsic(const struct intel_device_info *devinfo,
                                   nir_intrinsic_instr *instr)
{
   assert(devinfo->has_lsc);

   enum lsc_fence_scope scope = LSC_FENCE_LOCAL;
   enum lsc_flush_type flush_type = LSC_FLUSH_TYPE_NONE;

   if (nir_intrinsic_has_memory_scope(instr)) {
      switch (nir_intrinsic_memory_scope(instr)) {
      case SCOPE_DEVICE:
      case SCOPE_QUEUE_FAMILY:
         scope = LSC_FENCE_TILE;
         flush_type = LSC_FLUSH_TYPE_EVICT;
         break;
      case SCOPE_WORKGROUP:
         scope = LSC_FENCE_THREADGROUP;
         flush_type = LSC_FLUSH_TYPE_EVICT;
         break;
      case SCOPE_SHADER_CALL:
      case SCOPE_INVOCATION:
      case SCOPE_SUBGROUP:
      case SCOPE_NONE:
         break;
      }
   } else {
      /* No scope defined. */
      scope = LSC_FENCE_TILE;
      flush_type = LSC_FLUSH_TYPE_EVICT;
   }
   return lsc_fence_msg_desc(devinfo, scope, flush_type, true);
}

void
fs_visitor::nir_emit_intrinsic(const fs_builder &bld, nir_intrinsic_instr *instr)
{
   fs_reg dest;
   if (nir_intrinsic_infos[instr->intrinsic].has_dest)
      dest = get_nir_dest(instr->dest);

   switch (instr->intrinsic) {
   case nir_intrinsic_resource_intel:
      nir_ssa_bind_infos[instr->dest.ssa.index].valid = true;
      nir_ssa_bind_infos[instr->dest.ssa.index].bindless =
         (nir_intrinsic_resource_access_intel(instr) &
          nir_resource_intel_bindless) != 0;
      nir_ssa_bind_infos[instr->dest.ssa.index].block =
         nir_intrinsic_resource_block_intel(instr);
      nir_ssa_bind_infos[instr->dest.ssa.index].set =
         nir_intrinsic_desc_set(instr);
      nir_ssa_bind_infos[instr->dest.ssa.index].binding =
         nir_intrinsic_binding(instr);

      if ((nir_intrinsic_resource_access_intel(instr) &
           nir_resource_intel_non_uniform) ||
          !instr->src[1].is_ssa) {
         nir_resource_values[instr->dest.ssa.index] = fs_reg();
      } else {
         nir_resource_values[instr->dest.ssa.index] =
            try_rebuild_resource(bld, instr->src[1].ssa);
      }
      nir_ssa_values[instr->dest.ssa.index] =
         nir_ssa_values[instr->src[1].ssa->index];
      break;

   case nir_intrinsic_image_load:
   case nir_intrinsic_image_store:
   case nir_intrinsic_image_atomic:
   case nir_intrinsic_image_atomic_swap:
   case nir_intrinsic_bindless_image_load:
   case nir_intrinsic_bindless_image_store:
   case nir_intrinsic_bindless_image_atomic:
   case nir_intrinsic_bindless_image_atomic_swap: {
      /* Get some metadata from the image intrinsic. */
      const nir_intrinsic_info *info = &nir_intrinsic_infos[instr->intrinsic];

      fs_reg srcs[SURFACE_LOGICAL_NUM_SRCS];

      switch (instr->intrinsic) {
      case nir_intrinsic_image_load:
      case nir_intrinsic_image_store:
      case nir_intrinsic_image_atomic:
      case nir_intrinsic_image_atomic_swap:
         srcs[SURFACE_LOGICAL_SRC_SURFACE] =
            get_nir_image_intrinsic_image(bld, instr);
         break;

      default:
         /* Bindless */
         srcs[SURFACE_LOGICAL_SRC_SURFACE_HANDLE] =
            get_nir_image_intrinsic_image(bld, instr);
         break;
      }

      srcs[SURFACE_LOGICAL_SRC_ADDRESS] = get_nir_src(instr->src[1]);
      srcs[SURFACE_LOGICAL_SRC_IMM_DIMS] =
         brw_imm_ud(nir_image_intrinsic_coord_components(instr));

      /* Emit an image load, store or atomic op. */
      if (instr->intrinsic == nir_intrinsic_image_load ||
          instr->intrinsic == nir_intrinsic_bindless_image_load) {
         srcs[SURFACE_LOGICAL_SRC_IMM_ARG] = brw_imm_ud(instr->num_components);
         srcs[SURFACE_LOGICAL_SRC_ALLOW_SAMPLE_MASK] = brw_imm_ud(0);
         fs_inst *inst =
            bld.emit(SHADER_OPCODE_TYPED_SURFACE_READ_LOGICAL,
                     dest, srcs, SURFACE_LOGICAL_NUM_SRCS);
         inst->size_written = instr->num_components * dispatch_width * 4;
      } else if (instr->intrinsic == nir_intrinsic_image_store ||
                 instr->intrinsic == nir_intrinsic_bindless_image_store) {
         srcs[SURFACE_LOGICAL_SRC_IMM_ARG] = brw_imm_ud(instr->num_components);
         srcs[SURFACE_LOGICAL_SRC_DATA] = get_nir_src(instr->src[3]);
         srcs[SURFACE_LOGICAL_SRC_ALLOW_SAMPLE_MASK] = brw_imm_ud(1);
         bld.emit(SHADER_OPCODE_TYPED_SURFACE_WRITE_LOGICAL,
                  fs_reg(), srcs, SURFACE_LOGICAL_NUM_SRCS);
      } else {
         unsigned num_srcs = info->num_srcs;
         int op = lsc_aop_for_nir_intrinsic(instr);
         if (op == LSC_OP_ATOMIC_INC || op == LSC_OP_ATOMIC_DEC) {
            assert(num_srcs == 4);
            num_srcs = 3;
         }

         srcs[SURFACE_LOGICAL_SRC_IMM_ARG] = brw_imm_ud(op);

         fs_reg data;
         if (num_srcs >= 4)
            data = get_nir_src(instr->src[3]);
         if (num_srcs >= 5) {
            fs_reg tmp = bld.vgrf(data.type, 2);
            fs_reg sources[2] = { data, get_nir_src(instr->src[4]) };
            bld.LOAD_PAYLOAD(tmp, sources, 2, 0);
            data = tmp;
         }
         srcs[SURFACE_LOGICAL_SRC_DATA] = data;
         srcs[SURFACE_LOGICAL_SRC_ALLOW_SAMPLE_MASK] = brw_imm_ud(1);

         bld.emit(SHADER_OPCODE_TYPED_ATOMIC_LOGICAL,
                  dest, srcs, SURFACE_LOGICAL_NUM_SRCS);
      }
      break;
   }

   case nir_intrinsic_image_size:
   case nir_intrinsic_bindless_image_size: {
      /* Cube image sizes should have previously been lowered to a 2D array */
      assert(nir_intrinsic_image_dim(instr) != GLSL_SAMPLER_DIM_CUBE);

      /* Unlike the [un]typed load and store opcodes, the TXS that this turns
       * into will handle the binding table index for us in the geneerator.
       * Incidentally, this means that we can handle bindless with exactly the
       * same code.
       */
      fs_reg image = retype(get_nir_src_imm(instr->src[0]),
                            BRW_REGISTER_TYPE_UD);
      image = bld.emit_uniformize(image);

      assert(nir_src_as_uint(instr->src[1]) == 0);

      fs_reg srcs[TEX_LOGICAL_NUM_SRCS];
      if (instr->intrinsic == nir_intrinsic_image_size)
         srcs[TEX_LOGICAL_SRC_SURFACE] = image;
      else
         srcs[TEX_LOGICAL_SRC_SURFACE_HANDLE] = image;
      srcs[TEX_LOGICAL_SRC_SAMPLER] = brw_imm_d(0);
      srcs[TEX_LOGICAL_SRC_COORD_COMPONENTS] = brw_imm_d(0);
      srcs[TEX_LOGICAL_SRC_GRAD_COMPONENTS] = brw_imm_d(0);

      /* Since the image size is always uniform, we can just emit a SIMD8
       * query instruction and splat the result out.
       */
      const fs_builder ubld = bld.exec_all().group(8, 0);

      fs_reg tmp = ubld.vgrf(BRW_REGISTER_TYPE_UD, 4);
      fs_inst *inst = ubld.emit(SHADER_OPCODE_IMAGE_SIZE_LOGICAL,
                                tmp, srcs, ARRAY_SIZE(srcs));
      inst->size_written = 4 * REG_SIZE;

      for (unsigned c = 0; c < instr->dest.ssa.num_components; ++c) {
         bld.MOV(offset(retype(dest, tmp.type), bld, c),
                 component(offset(tmp, ubld, c), 0));
      }
      break;
   }

   case nir_intrinsic_image_load_raw_intel: {
      fs_reg srcs[SURFACE_LOGICAL_NUM_SRCS];
      srcs[SURFACE_LOGICAL_SRC_SURFACE] =
         get_nir_image_intrinsic_image(bld, instr);
      srcs[SURFACE_LOGICAL_SRC_ADDRESS] = get_nir_src(instr->src[1]);
      srcs[SURFACE_LOGICAL_SRC_IMM_DIMS] = brw_imm_ud(1);
      srcs[SURFACE_LOGICAL_SRC_IMM_ARG] = brw_imm_ud(instr->num_components);
      srcs[SURFACE_LOGICAL_SRC_ALLOW_SAMPLE_MASK] = brw_imm_ud(0);

      fs_inst *inst =
         bld.emit(SHADER_OPCODE_UNTYPED_SURFACE_READ_LOGICAL,
                  dest, srcs, SURFACE_LOGICAL_NUM_SRCS);
      inst->size_written = instr->num_components * dispatch_width * 4;
      break;
   }

   case nir_intrinsic_image_store_raw_intel: {
      fs_reg srcs[SURFACE_LOGICAL_NUM_SRCS];
      srcs[SURFACE_LOGICAL_SRC_SURFACE] =
         get_nir_image_intrinsic_image(bld, instr);
      srcs[SURFACE_LOGICAL_SRC_ADDRESS] = get_nir_src(instr->src[1]);
      srcs[SURFACE_LOGICAL_SRC_DATA] = get_nir_src(instr->src[2]);
      srcs[SURFACE_LOGICAL_SRC_IMM_DIMS] = brw_imm_ud(1);
      srcs[SURFACE_LOGICAL_SRC_IMM_ARG] = brw_imm_ud(instr->num_components);
      srcs[SURFACE_LOGICAL_SRC_ALLOW_SAMPLE_MASK] = brw_imm_ud(1);

      bld.emit(SHADER_OPCODE_UNTYPED_SURFACE_WRITE_LOGICAL,
               fs_reg(), srcs, SURFACE_LOGICAL_NUM_SRCS);
      break;
   }

   case nir_intrinsic_scoped_barrier:
   case nir_intrinsic_begin_invocation_interlock:
   case nir_intrinsic_end_invocation_interlock: {
      bool ugm_fence, slm_fence, tgm_fence, urb_fence;
      enum opcode opcode = BRW_OPCODE_NOP;

      /* Handling interlock intrinsics here will allow the logic for IVB
       * render cache (see below) to be reused.
       */

      switch (instr->intrinsic) {
      case nir_intrinsic_scoped_barrier: {
         /* Note we only care about the memory part of the
          * barrier.  The execution part will be taken care
          * of by the stage specific intrinsic handler functions.
          */
         nir_variable_mode modes = nir_intrinsic_memory_modes(instr);
         ugm_fence = modes & (nir_var_mem_ssbo | nir_var_mem_global);
         slm_fence = modes & nir_var_mem_shared;
         tgm_fence = modes & nir_var_image;
         urb_fence = modes & (nir_var_shader_out | nir_var_mem_task_payload);
         if (nir_intrinsic_memory_scope(instr) != SCOPE_NONE)
            opcode = SHADER_OPCODE_MEMORY_FENCE;
         break;
      }

      case nir_intrinsic_begin_invocation_interlock:
         /* For beginInvocationInterlockARB(), we will generate a memory fence
          * but with a different opcode so that generator can pick SENDC
          * instead of SEND.
          */
         assert(stage == MESA_SHADER_FRAGMENT);
         ugm_fence = tgm_fence = true;
         slm_fence = urb_fence = false;
         opcode = SHADER_OPCODE_INTERLOCK;
         break;

      case nir_intrinsic_end_invocation_interlock:
         /* For endInvocationInterlockARB(), we need to insert a memory fence which
          * stalls in the shader until the memory transactions prior to that
          * fence are complete.  This ensures that the shader does not end before
          * any writes from its critical section have landed.  Otherwise, you can
          * end up with a case where the next invocation on that pixel properly
          * stalls for previous FS invocation on its pixel to complete but
          * doesn't actually wait for the dataport memory transactions from that
          * thread to land before submitting its own.
          */
         assert(stage == MESA_SHADER_FRAGMENT);
         ugm_fence = tgm_fence = true;
         slm_fence = urb_fence = false;
         opcode = SHADER_OPCODE_MEMORY_FENCE;
         break;

      default:
         unreachable("invalid intrinsic");
      }

      if (opcode == BRW_OPCODE_NOP)
         break;

      if (nir->info.shared_size > 0) {
         assert(gl_shader_stage_uses_workgroup(stage));
      } else {
         slm_fence = false;
      }

      /* If the workgroup fits in a single HW thread, the messages for SLM are
       * processed in-order and the shader itself is already synchronized so
       * the memory fence is not necessary.
       *
       * TODO: Check if applies for many HW threads sharing same Data Port.
       */
      if (!nir->info.workgroup_size_variable &&
          slm_fence && workgroup_size() <= dispatch_width)
         slm_fence = false;

      switch (stage) {
         case MESA_SHADER_TESS_CTRL:
         case MESA_SHADER_TASK:
         case MESA_SHADER_MESH:
            break;
         default:
            urb_fence = false;
            break;
      }

      unsigned fence_regs_count = 0;
      fs_reg fence_regs[4] = {};

      const fs_builder ubld = bld.group(8, 0);

      /* A memory barrier with acquire semantics requires us to
       * guarantee that memory operations of the specified storage
       * class sequenced-after the barrier aren't reordered before the
       * barrier, nor before any previous atomic operation
       * sequenced-before the barrier which may be synchronizing this
       * acquire barrier with a prior release sequence.
       *
       * In order to guarantee the latter we must make sure that any
       * such previous operation has completed execution before
       * invalidating the relevant caches, since otherwise some cache
       * could be polluted by a concurrent thread after its
       * invalidation but before the previous atomic completes, which
       * could lead to a violation of the expected memory ordering if
       * a subsequent memory read hits the polluted cacheline, which
       * would return a stale value read from memory before the
       * completion of the atomic sequenced-before the barrier.
       *
       * This ordering inversion can be avoided trivially if the
       * operations we need to order are all handled by a single
       * in-order cache, since the flush implied by the memory fence
       * occurs after any pending operations have completed, however
       * that doesn't help us when dealing with multiple caches
       * processing requests out of order, in which case we need to
       * explicitly stall the EU until any pending memory operations
       * have executed.
       *
       * Note that that might be somewhat heavy handed in some cases.
       * In particular when this memory fence was inserted by
       * spirv_to_nir() lowering an atomic with acquire semantics into
       * an atomic+barrier sequence we could do a better job by
       * synchronizing with respect to that one atomic *only*, but
       * that would require additional information not currently
       * available to the backend.
       *
       * XXX - Use an alternative workaround on IVB and ICL, since
       *       SYNC.ALLWR is only available on Gfx12+.
       */
      if (devinfo->ver >= 12 &&
          (!nir_intrinsic_has_memory_scope(instr) ||
           (nir_intrinsic_memory_semantics(instr) & NIR_MEMORY_ACQUIRE))) {
         ubld.exec_all().group(1, 0).emit(
            BRW_OPCODE_SYNC, ubld.null_reg_ud(), brw_imm_ud(TGL_SYNC_ALLWR));
      }

      if (devinfo->has_lsc) {
         assert(devinfo->verx10 >= 125);
         uint32_t desc =
            lsc_fence_descriptor_for_intrinsic(devinfo, instr);
         if (ugm_fence) {
            fence_regs[fence_regs_count++] =
               emit_fence(ubld, opcode, GFX12_SFID_UGM, desc,
                          true /* commit_enable */,
                          0 /* bti; ignored for LSC */);
         }

         if (tgm_fence) {
            fence_regs[fence_regs_count++] =
               emit_fence(ubld, opcode, GFX12_SFID_TGM, desc,
                          true /* commit_enable */,
                          0 /* bti; ignored for LSC */);
         }

         if (slm_fence) {
            assert(opcode == SHADER_OPCODE_MEMORY_FENCE);
            if (intel_needs_workaround(devinfo, 14014063774)) {
               /* Wa_14014063774
                *
                * Before SLM fence compiler needs to insert SYNC.ALLWR in order
                * to avoid the SLM data race.
                */
               ubld.exec_all().group(1, 0).emit(
                  BRW_OPCODE_SYNC, ubld.null_reg_ud(),
                  brw_imm_ud(TGL_SYNC_ALLWR));
            }
            fence_regs[fence_regs_count++] =
               emit_fence(ubld, opcode, GFX12_SFID_SLM, desc,
                          true /* commit_enable */,
                          0 /* BTI; ignored for LSC */);
         }

         if (urb_fence) {
            assert(opcode == SHADER_OPCODE_MEMORY_FENCE);
            fence_regs[fence_regs_count++] =
               emit_fence(ubld, opcode, BRW_SFID_URB, desc,
                          true /* commit_enable */,
                          0 /* BTI; ignored for LSC */);
         }
      } else if (devinfo->ver >= 11) {
         if (tgm_fence || ugm_fence || urb_fence) {
            fence_regs[fence_regs_count++] =
               emit_fence(ubld, opcode, GFX7_SFID_DATAPORT_DATA_CACHE, 0,
                          true /* commit_enable HSD ES # 1404612949 */,
                          0 /* BTI = 0 means data cache */);
         }

         if (slm_fence) {
            assert(opcode == SHADER_OPCODE_MEMORY_FENCE);
            fence_regs[fence_regs_count++] =
               emit_fence(ubld, opcode, GFX7_SFID_DATAPORT_DATA_CACHE, 0,
                          true /* commit_enable HSD ES # 1404612949 */,
                          GFX7_BTI_SLM);
         }
      } else {
         /* Prior to Icelake, they're all lumped into a single cache except on
          * Ivy Bridge and Bay Trail where typed messages actually go through
          * the render cache.  There, we need both fences because we may
          * access storage images as either typed or untyped.
          */
         const bool render_fence = tgm_fence && devinfo->verx10 == 70;

         /* Simulation also complains on Gfx9 if we do not enable commit.
          */
         const bool commit_enable = render_fence ||
            instr->intrinsic == nir_intrinsic_end_invocation_interlock ||
            devinfo->ver == 9;

         if (tgm_fence || ugm_fence || slm_fence || urb_fence) {
            fence_regs[fence_regs_count++] =
               emit_fence(ubld, opcode, GFX7_SFID_DATAPORT_DATA_CACHE, 0,
                          commit_enable, 0 /* BTI */);
         }

         if (render_fence) {
            fence_regs[fence_regs_count++] =
               emit_fence(ubld, opcode, GFX6_SFID_DATAPORT_RENDER_CACHE, 0,
                          commit_enable, /* bti */ 0);
         }
      }

      assert(fence_regs_count <= ARRAY_SIZE(fence_regs));

      /* Be conservative in Gen11+ and always stall in a fence.  Since
       * there are two different fences, and shader might want to
       * synchronize between them.
       *
       * TODO: Use scope and visibility information for the barriers from NIR
       * to make a better decision on whether we need to stall.
       */
      bool force_stall = devinfo->ver >= 11;

      /* There are four cases where we want to insert a stall:
       *
       *  1. If we're a nir_intrinsic_end_invocation_interlock.  This is
       *     required to ensure that the shader EOT doesn't happen until
       *     after the fence returns.  Otherwise, we might end up with the
       *     next shader invocation for that pixel not respecting our fence
       *     because it may happen on a different HW thread.
       *
       *  2. If we have multiple fences.  This is required to ensure that
       *     they all complete and nothing gets weirdly out-of-order.
       *
       *  3. If we have no fences.  In this case, we need at least a
       *     scheduling barrier to keep the compiler from moving things
       *     around in an invalid way.
       *
       *  4. On Gen11+ and platforms with LSC, we have multiple fence types,
       *     without further information about the fence, we need to force a
       *     stall.
       */
      if (instr->intrinsic == nir_intrinsic_end_invocation_interlock ||
          fence_regs_count != 1 || devinfo->has_lsc || force_stall) {
         ubld.exec_all().group(1, 0).emit(
            FS_OPCODE_SCHEDULING_FENCE, ubld.null_reg_ud(),
            fence_regs, fence_regs_count);
      }

      break;
   }

   case nir_intrinsic_shader_clock: {
      /* We cannot do anything if there is an event, so ignore it for now */
      const fs_reg shader_clock = get_timestamp(bld);
      const fs_reg srcs[] = { component(shader_clock, 0),
                              component(shader_clock, 1) };
      bld.LOAD_PAYLOAD(dest, srcs, ARRAY_SIZE(srcs), 0);
      break;
   }

   case nir_intrinsic_load_reloc_const_intel: {
      uint32_t id = nir_intrinsic_param_idx(instr);

      /* Emit the reloc in the smallest SIMD size to limit register usage. */
      const fs_builder ubld = bld.exec_all().group(1, 0);
      fs_reg small_dest = ubld.vgrf(dest.type);
      ubld.UNDEF(small_dest);
      ubld.exec_all().group(1, 0).emit(SHADER_OPCODE_MOV_RELOC_IMM,
                                       small_dest, brw_imm_ud(id));

      /* Copy propagation will get rid of this MOV. */
      bld.MOV(dest, component(small_dest, 0));
      break;
   }

   case nir_intrinsic_load_uniform: {
      /* Offsets are in bytes but they should always aligned to
       * the type size
       */
      unsigned base_offset = nir_intrinsic_base(instr);
      assert(base_offset % 4 == 0 || base_offset % type_sz(dest.type) == 0);

      fs_reg src(UNIFORM, base_offset / 4, dest.type);

      if (nir_src_is_const(instr->src[0])) {
         unsigned load_offset = nir_src_as_uint(instr->src[0]);
         assert(load_offset % type_sz(dest.type) == 0);
         /* The base offset can only handle 32-bit units, so for 16-bit
          * data take the modulo of the offset with 4 bytes and add it to
          * the offset to read from within the source register.
          */
         src.offset = load_offset + base_offset % 4;

         for (unsigned j = 0; j < instr->num_components; j++) {
            bld.MOV(offset(dest, bld, j), offset(src, bld, j));
         }
      } else {
         fs_reg indirect = retype(get_nir_src(instr->src[0]),
                                  BRW_REGISTER_TYPE_UD);

         /* We need to pass a size to the MOV_INDIRECT but we don't want it to
          * go past the end of the uniform.  In order to keep the n'th
          * component from running past, we subtract off the size of all but
          * one component of the vector.
          */
         assert(nir_intrinsic_range(instr) >=
                instr->num_components * type_sz(dest.type));
         unsigned read_size = nir_intrinsic_range(instr) -
            (instr->num_components - 1) * type_sz(dest.type);

         bool supports_64bit_indirects =
            devinfo->platform != INTEL_PLATFORM_CHV && !intel_device_info_is_9lp(devinfo);

         if (type_sz(dest.type) != 8 || supports_64bit_indirects) {
            for (unsigned j = 0; j < instr->num_components; j++) {
               bld.emit(SHADER_OPCODE_MOV_INDIRECT,
                        offset(dest, bld, j), offset(src, bld, j),
                        indirect, brw_imm_ud(read_size));
            }
         } else {
            const unsigned num_mov_indirects =
               type_sz(dest.type) / type_sz(BRW_REGISTER_TYPE_UD);
            /* We read a little bit less per MOV INDIRECT, as they are now
             * 32-bits ones instead of 64-bit. Fix read_size then.
             */
            const unsigned read_size_32bit = read_size -
                (num_mov_indirects - 1) * type_sz(BRW_REGISTER_TYPE_UD);
            for (unsigned j = 0; j < instr->num_components; j++) {
               for (unsigned i = 0; i < num_mov_indirects; i++) {
                  bld.emit(SHADER_OPCODE_MOV_INDIRECT,
                           subscript(offset(dest, bld, j), BRW_REGISTER_TYPE_UD, i),
                           subscript(offset(src, bld, j), BRW_REGISTER_TYPE_UD, i),
                           indirect, brw_imm_ud(read_size_32bit));
               }
            }
         }
      }
      break;
   }

   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_ubo_uniform_block_intel: {
      fs_reg surface, surface_handle;

      if (get_nir_src_bindless(instr->src[0]))
         surface_handle = get_nir_buffer_intrinsic_index(bld, instr);
      else
         surface = get_nir_buffer_intrinsic_index(bld, instr);

      if (!nir_src_is_const(instr->src[1])) {
         if (instr->intrinsic == nir_intrinsic_load_ubo) {
            /* load_ubo with non-uniform offset */
            fs_reg base_offset = retype(get_nir_src(instr->src[1]),
                                        BRW_REGISTER_TYPE_UD);

            for (int i = 0; i < instr->num_components; i++)
               VARYING_PULL_CONSTANT_LOAD(bld, offset(dest, bld, i),
                                          surface, surface_handle,
                                          base_offset, i * type_sz(dest.type),
                                          nir_dest_bit_size(instr->dest) / 8);

            prog_data->has_ubo_pull = true;
         } else {
            /* load_ubo with uniform offset */
            const fs_builder ubld1 = bld.exec_all().group(1, 0);
            const fs_builder ubld8 = bld.exec_all().group(8, 0);
            const fs_builder ubld16 = bld.exec_all().group(16, 0);

            fs_reg srcs[SURFACE_LOGICAL_NUM_SRCS];

            srcs[SURFACE_LOGICAL_SRC_SURFACE]        = surface;
            srcs[SURFACE_LOGICAL_SRC_SURFACE_HANDLE] = surface_handle;

            const nir_src load_offset = instr->src[1];
            if (nir_src_is_const(load_offset)) {
               fs_reg addr = ubld8.vgrf(BRW_REGISTER_TYPE_UD);
               ubld8.MOV(addr, brw_imm_ud(nir_src_as_uint(load_offset)));
               srcs[SURFACE_LOGICAL_SRC_ADDRESS] = component(addr, 0);
            } else {
               srcs[SURFACE_LOGICAL_SRC_ADDRESS] =
                  bld.emit_uniformize(get_nir_src(load_offset));
            }

            const unsigned total_dwords = ALIGN(instr->num_components, REG_SIZE / 4);
            unsigned loaded_dwords = 0;

            const fs_reg packed_consts =
               ubld1.vgrf(BRW_REGISTER_TYPE_UD, total_dwords);

            while (loaded_dwords < total_dwords) {
               const unsigned block =
                  choose_oword_block_size_dwords(devinfo,
                                                 total_dwords - loaded_dwords);
               const unsigned block_bytes = block * 4;

               srcs[SURFACE_LOGICAL_SRC_IMM_ARG] = brw_imm_ud(block);

               const fs_builder &ubld = block <= 8 ? ubld8 : ubld16;
               ubld.emit(SHADER_OPCODE_UNALIGNED_OWORD_BLOCK_READ_LOGICAL,
                         retype(byte_offset(packed_consts, loaded_dwords * 4), BRW_REGISTER_TYPE_UD),
                         srcs, SURFACE_LOGICAL_NUM_SRCS)->size_written = align(block_bytes, REG_SIZE);

               loaded_dwords += block;

               ubld1.ADD(srcs[SURFACE_LOGICAL_SRC_ADDRESS],
                         srcs[SURFACE_LOGICAL_SRC_ADDRESS],
                         brw_imm_ud(block_bytes));
            }

            for (unsigned c = 0; c < instr->num_components; c++) {
               bld.MOV(retype(offset(dest, bld, c), BRW_REGISTER_TYPE_UD),
                       component(packed_consts, c));
            }

            prog_data->has_ubo_pull = true;
         }
      } else {
         /* Even if we are loading doubles, a pull constant load will load
          * a 32-bit vec4, so should only reserve vgrf space for that. If we
          * need to load a full dvec4 we will have to emit 2 loads. This is
          * similar to demote_pull_constants(), except that in that case we
          * see individual accesses to each component of the vector and then
          * we let CSE deal with duplicate loads. Here we see a vector access
          * and we have to split it if necessary.
          */
         const unsigned type_size = type_sz(dest.type);
         const unsigned load_offset = nir_src_as_uint(instr->src[1]);
         const unsigned ubo_block =
            brw_nir_ubo_surface_index_get_push_block(instr->src[0]);
         const unsigned offset_256b = load_offset / 32;

         /* See if we've selected this as a push constant candidate */
         fs_reg push_reg;
         for (int i = 0; i < 4; i++) {
            const struct brw_ubo_range *range = &prog_data->ubo_ranges[i];
            if (range->block == ubo_block &&
                offset_256b >= range->start &&
                offset_256b < range->start + range->length) {

               push_reg = fs_reg(UNIFORM, UBO_START + i, dest.type);
               push_reg.offset = load_offset - 32 * range->start;
               break;
            }
         }

         if (push_reg.file != BAD_FILE) {
            for (unsigned i = 0; i < instr->num_components; i++) {
               bld.MOV(offset(dest, bld, i),
                       byte_offset(push_reg, i * type_size));
            }
            break;
         }

         prog_data->has_ubo_pull = true;

         const unsigned block_sz = 64; /* Fetch one cacheline at a time. */
         const fs_builder ubld = bld.exec_all().group(block_sz / 4, 0);

         for (unsigned c = 0; c < instr->num_components;) {
            const unsigned base = load_offset + c * type_size;
            /* Number of usable components in the next block-aligned load. */
            const unsigned count = MIN2(instr->num_components - c,
                                        (block_sz - base % block_sz) / type_size);

            const fs_reg packed_consts = ubld.vgrf(BRW_REGISTER_TYPE_UD);
            fs_reg srcs[PULL_UNIFORM_CONSTANT_SRCS];
            srcs[PULL_UNIFORM_CONSTANT_SRC_SURFACE]        = surface;
            srcs[PULL_UNIFORM_CONSTANT_SRC_SURFACE_HANDLE] = surface_handle;
            srcs[PULL_UNIFORM_CONSTANT_SRC_OFFSET]         = brw_imm_ud(base & ~(block_sz - 1));
            srcs[PULL_UNIFORM_CONSTANT_SRC_SIZE]           = brw_imm_ud(block_sz);

            ubld.emit(FS_OPCODE_UNIFORM_PULL_CONSTANT_LOAD, packed_consts,
                      srcs, PULL_UNIFORM_CONSTANT_SRCS);

            const fs_reg consts =
               retype(byte_offset(packed_consts, base & (block_sz - 1)),
                      dest.type);

            for (unsigned d = 0; d < count; d++)
               bld.MOV(offset(dest, bld, c + d), component(consts, d));

            c += count;
         }
      }
      break;
   }

   case nir_intrinsic_load_global:
   case nir_intrinsic_load_global_constant: {
      assert(devinfo->ver >= 8);

      assert(nir_dest_bit_size(instr->dest) <= 32);
      assert(nir_intrinsic_align(instr) > 0);
      fs_reg srcs[A64_LOGICAL_NUM_SRCS];
      srcs[A64_LOGICAL_ADDRESS] = get_nir_src(instr->src[0]);
      srcs[A64_LOGICAL_SRC] = fs_reg(); /* No source data */
      srcs[A64_LOGICAL_ENABLE_HELPERS] =
         brw_imm_ud(nir_intrinsic_access(instr) & ACCESS_INCLUDE_HELPERS);

      if (nir_dest_bit_size(instr->dest) == 32 &&
          nir_intrinsic_align(instr) >= 4) {
         assert(nir_dest_num_components(instr->dest) <= 4);

         srcs[A64_LOGICAL_ARG] = brw_imm_ud(instr->num_components);

         fs_inst *inst =
            bld.emit(SHADER_OPCODE_A64_UNTYPED_READ_LOGICAL, dest,
                     srcs, A64_LOGICAL_NUM_SRCS);
         inst->size_written = instr->num_components *
                              inst->dst.component_size(inst->exec_size);
      } else {
         const unsigned bit_size = nir_dest_bit_size(instr->dest);
         assert(nir_dest_num_components(instr->dest) == 1);
         fs_reg tmp = bld.vgrf(BRW_REGISTER_TYPE_UD);

         srcs[A64_LOGICAL_ARG] = brw_imm_ud(bit_size);

         bld.emit(SHADER_OPCODE_A64_BYTE_SCATTERED_READ_LOGICAL, tmp,
                  srcs, A64_LOGICAL_NUM_SRCS);
         bld.MOV(dest, subscript(tmp, dest.type, 0));
      }
      break;
   }

   case nir_intrinsic_store_global: {
      assert(devinfo->ver >= 8);

      assert(nir_src_bit_size(instr->src[0]) <= 32);
      assert(nir_intrinsic_write_mask(instr) ==
             (1u << instr->num_components) - 1);
      assert(nir_intrinsic_align(instr) > 0);

      fs_reg srcs[A64_LOGICAL_NUM_SRCS];
      srcs[A64_LOGICAL_ADDRESS] = get_nir_src(instr->src[1]);
      srcs[A64_LOGICAL_ENABLE_HELPERS] =
         brw_imm_ud(nir_intrinsic_access(instr) & ACCESS_INCLUDE_HELPERS);

      if (nir_src_bit_size(instr->src[0]) == 32 &&
          nir_intrinsic_align(instr) >= 4) {
         assert(nir_src_num_components(instr->src[0]) <= 4);

         srcs[A64_LOGICAL_SRC] = get_nir_src(instr->src[0]); /* Data */
         srcs[A64_LOGICAL_ARG] = brw_imm_ud(instr->num_components);

         bld.emit(SHADER_OPCODE_A64_UNTYPED_WRITE_LOGICAL, fs_reg(),
                  srcs, A64_LOGICAL_NUM_SRCS);
      } else {
         assert(nir_src_num_components(instr->src[0]) == 1);
         const unsigned bit_size = nir_src_bit_size(instr->src[0]);
         brw_reg_type data_type =
            brw_reg_type_from_bit_size(bit_size, BRW_REGISTER_TYPE_UD);
         fs_reg tmp = bld.vgrf(BRW_REGISTER_TYPE_UD);
         bld.MOV(tmp, retype(get_nir_src(instr->src[0]), data_type));

         srcs[A64_LOGICAL_SRC] = tmp;
         srcs[A64_LOGICAL_ARG] = brw_imm_ud(bit_size);

         bld.emit(SHADER_OPCODE_A64_BYTE_SCATTERED_WRITE_LOGICAL, fs_reg(),
                  srcs, A64_LOGICAL_NUM_SRCS);
      }
      break;
   }

   case nir_intrinsic_global_atomic:
   case nir_intrinsic_global_atomic_swap:
      nir_emit_global_atomic(bld, instr);
      break;

   case nir_intrinsic_load_global_const_block_intel: {
      assert(nir_dest_bit_size(instr->dest) == 32);
      assert(instr->num_components == 8 || instr->num_components == 16);

      const fs_builder ubld = bld.exec_all().group(instr->num_components, 0);
      fs_reg load_val;

      bool is_pred_const = nir_src_is_const(instr->src[1]);
      if (is_pred_const && nir_src_as_uint(instr->src[1]) == 0) {
         /* In this case, we don't want the UBO load at all.  We really
          * shouldn't get here but it's possible.
          */
         load_val = brw_imm_ud(0);
      } else {
         /* The uniform process may stomp the flag so do this first */
         fs_reg addr = bld.emit_uniformize(get_nir_src(instr->src[0]));

         load_val = ubld.vgrf(BRW_REGISTER_TYPE_UD);

         /* If the predicate is constant and we got here, then it's non-zero
          * and we don't need the predicate at all.
          */
         if (!is_pred_const) {
            /* Load the predicate */
            fs_reg pred = bld.emit_uniformize(get_nir_src(instr->src[1]));
            fs_inst *mov = ubld.MOV(bld.null_reg_d(), pred);
            mov->conditional_mod = BRW_CONDITIONAL_NZ;

            /* Stomp the destination with 0 if we're OOB */
            mov = ubld.MOV(load_val, brw_imm_ud(0));
            mov->predicate = BRW_PREDICATE_NORMAL;
            mov->predicate_inverse = true;
         }

         fs_reg srcs[A64_LOGICAL_NUM_SRCS];
         srcs[A64_LOGICAL_ADDRESS] = addr;
         srcs[A64_LOGICAL_SRC] = fs_reg(); /* No source data */
         srcs[A64_LOGICAL_ARG] = brw_imm_ud(instr->num_components);
         /* This intrinsic loads memory from a uniform address, sometimes
          * shared across lanes. We never need to mask it.
          */
         srcs[A64_LOGICAL_ENABLE_HELPERS] = brw_imm_ud(0);

         fs_inst *load = ubld.emit(SHADER_OPCODE_A64_OWORD_BLOCK_READ_LOGICAL,
                                   load_val, srcs, A64_LOGICAL_NUM_SRCS);
         if (!is_pred_const)
            load->predicate = BRW_PREDICATE_NORMAL;
      }

      /* From the HW perspective, we just did a single SIMD16 instruction
       * which loaded a dword in each SIMD channel.  From NIR's perspective,
       * this instruction returns a vec16.  Any users of this data in the
       * back-end will expect a vec16 per SIMD channel so we have to emit a
       * pile of MOVs to resolve this discrepancy.  Fortunately, copy-prop
       * will generally clean them up for us.
       */
      for (unsigned i = 0; i < instr->num_components; i++) {
         bld.MOV(retype(offset(dest, bld, i), BRW_REGISTER_TYPE_UD),
                 component(load_val, i));
      }
      break;
   }

   case nir_intrinsic_load_global_constant_uniform_block_intel: {
      const unsigned total_dwords = ALIGN(instr->num_components, REG_SIZE / 4);
      unsigned loaded_dwords = 0;

      const fs_builder ubld1 = bld.exec_all().group(1, 0);
      const fs_builder ubld8 = bld.exec_all().group(8, 0);
      const fs_builder ubld16 = bld.exec_all().group(16, 0);

      const fs_reg packed_consts =
         ubld1.vgrf(BRW_REGISTER_TYPE_UD, total_dwords);
      fs_reg address = bld.emit_uniformize(get_nir_src(instr->src[0]));

      while (loaded_dwords < total_dwords) {
         const unsigned block =
            choose_oword_block_size_dwords(devinfo,
                                           total_dwords - loaded_dwords);
         const unsigned block_bytes = block * 4;

         const fs_builder &ubld = block <= 8 ? ubld8 : ubld16;

         fs_reg srcs[A64_LOGICAL_NUM_SRCS];
         srcs[A64_LOGICAL_ADDRESS] = address;
         srcs[A64_LOGICAL_SRC] = fs_reg(); /* No source data */
         srcs[A64_LOGICAL_ARG] = brw_imm_ud(block);
         srcs[A64_LOGICAL_ENABLE_HELPERS] = brw_imm_ud(0);
         ubld.emit(SHADER_OPCODE_A64_UNALIGNED_OWORD_BLOCK_READ_LOGICAL,
                   retype(byte_offset(packed_consts, loaded_dwords * 4), BRW_REGISTER_TYPE_UD),
                   srcs, A64_LOGICAL_NUM_SRCS)->size_written = block_bytes;

         increment_a64_address(ubld1, address, block_bytes);
         loaded_dwords += block;
      }

      for (unsigned c = 0; c < instr->num_components; c++)
         bld.MOV(retype(offset(dest, bld, c), BRW_REGISTER_TYPE_UD),
                 component(packed_consts, c));

      break;
   }

   case nir_intrinsic_load_ssbo: {
      assert(devinfo->ver >= 7);

      const unsigned bit_size = nir_dest_bit_size(instr->dest);
      fs_reg srcs[SURFACE_LOGICAL_NUM_SRCS];
      srcs[get_nir_src_bindless(instr->src[0]) ?
           SURFACE_LOGICAL_SRC_SURFACE_HANDLE :
           SURFACE_LOGICAL_SRC_SURFACE] =
         get_nir_buffer_intrinsic_index(bld, instr);
      srcs[SURFACE_LOGICAL_SRC_ADDRESS] = get_nir_src(instr->src[1]);
      srcs[SURFACE_LOGICAL_SRC_IMM_DIMS] = brw_imm_ud(1);
      srcs[SURFACE_LOGICAL_SRC_ALLOW_SAMPLE_MASK] = brw_imm_ud(0);

      /* Make dest unsigned because that's what the temporary will be */
      dest.type = brw_reg_type_from_bit_size(bit_size, BRW_REGISTER_TYPE_UD);

      /* Read the vector */
      assert(bit_size <= 32);
      assert(nir_intrinsic_align(instr) > 0);
      if (bit_size == 32 &&
          nir_intrinsic_align(instr) >= 4) {
         assert(nir_dest_num_components(instr->dest) <= 4);
         srcs[SURFACE_LOGICAL_SRC_IMM_ARG] = brw_imm_ud(instr->num_components);
         fs_inst *inst =
            bld.emit(SHADER_OPCODE_UNTYPED_SURFACE_READ_LOGICAL,
                     dest, srcs, SURFACE_LOGICAL_NUM_SRCS);
         inst->size_written = instr->num_components * dispatch_width * 4;
      } else {
         assert(nir_dest_num_components(instr->dest) == 1);
         srcs[SURFACE_LOGICAL_SRC_IMM_ARG] = brw_imm_ud(bit_size);

         fs_reg read_result = bld.vgrf(BRW_REGISTER_TYPE_UD);
         bld.emit(SHADER_OPCODE_BYTE_SCATTERED_READ_LOGICAL,
                  read_result, srcs, SURFACE_LOGICAL_NUM_SRCS);
         bld.MOV(dest, subscript(read_result, dest.type, 0));
      }
      break;
   }

   case nir_intrinsic_store_ssbo: {
      assert(devinfo->ver >= 7);

      const unsigned bit_size = nir_src_bit_size(instr->src[0]);
      fs_reg srcs[SURFACE_LOGICAL_NUM_SRCS];
      srcs[get_nir_src_bindless(instr->src[1]) ?
           SURFACE_LOGICAL_SRC_SURFACE_HANDLE :
           SURFACE_LOGICAL_SRC_SURFACE] =
         get_nir_buffer_intrinsic_index(bld, instr);
      srcs[SURFACE_LOGICAL_SRC_ADDRESS] = get_nir_src(instr->src[2]);
      srcs[SURFACE_LOGICAL_SRC_IMM_DIMS] = brw_imm_ud(1);
      srcs[SURFACE_LOGICAL_SRC_ALLOW_SAMPLE_MASK] = brw_imm_ud(1);

      fs_reg data = get_nir_src(instr->src[0]);
      data.type = brw_reg_type_from_bit_size(bit_size, BRW_REGISTER_TYPE_UD);

      assert(bit_size <= 32);
      assert(nir_intrinsic_write_mask(instr) ==
             (1u << instr->num_components) - 1);
      assert(nir_intrinsic_align(instr) > 0);
      if (bit_size == 32 &&
          nir_intrinsic_align(instr) >= 4) {
         assert(nir_src_num_components(instr->src[0]) <= 4);
         srcs[SURFACE_LOGICAL_SRC_DATA] = data;
         srcs[SURFACE_LOGICAL_SRC_IMM_ARG] = brw_imm_ud(instr->num_components);
         bld.emit(SHADER_OPCODE_UNTYPED_SURFACE_WRITE_LOGICAL,
                  fs_reg(), srcs, SURFACE_LOGICAL_NUM_SRCS);
      } else {
         assert(nir_src_num_components(instr->src[0]) == 1);
         srcs[SURFACE_LOGICAL_SRC_IMM_ARG] = brw_imm_ud(bit_size);

         srcs[SURFACE_LOGICAL_SRC_DATA] = bld.vgrf(BRW_REGISTER_TYPE_UD);
         bld.MOV(srcs[SURFACE_LOGICAL_SRC_DATA], data);

         bld.emit(SHADER_OPCODE_BYTE_SCATTERED_WRITE_LOGICAL,
                  fs_reg(), srcs, SURFACE_LOGICAL_NUM_SRCS);
      }
      break;
   }

   case nir_intrinsic_load_ssbo_uniform_block_intel:
   case nir_intrinsic_load_shared_uniform_block_intel: {
      fs_reg srcs[SURFACE_LOGICAL_NUM_SRCS];

      const bool is_ssbo =
         instr->intrinsic == nir_intrinsic_load_ssbo_uniform_block_intel;
      if (is_ssbo) {
         srcs[get_nir_src_bindless(instr->src[0]) ?
              SURFACE_LOGICAL_SRC_SURFACE_HANDLE :
              SURFACE_LOGICAL_SRC_SURFACE] =
            get_nir_buffer_intrinsic_index(bld, instr);
      } else {
         srcs[SURFACE_LOGICAL_SRC_SURFACE] = fs_reg(brw_imm_ud(GFX7_BTI_SLM));
      }

      const unsigned total_dwords = ALIGN(instr->num_components, REG_SIZE / 4);
      unsigned loaded_dwords = 0;

      const fs_builder ubld1 = bld.exec_all().group(1, 0);
      const fs_builder ubld8 = bld.exec_all().group(8, 0);
      const fs_builder ubld16 = bld.exec_all().group(16, 0);

      const fs_reg packed_consts =
         ubld1.vgrf(BRW_REGISTER_TYPE_UD, total_dwords);

      const nir_src load_offset = is_ssbo ? instr->src[1] : instr->src[0];
      if (nir_src_is_const(load_offset)) {
         fs_reg addr = ubld8.vgrf(BRW_REGISTER_TYPE_UD);
         ubld8.MOV(addr, brw_imm_ud(nir_src_as_uint(load_offset)));
         srcs[SURFACE_LOGICAL_SRC_ADDRESS] = component(addr, 0);
      } else {
         srcs[SURFACE_LOGICAL_SRC_ADDRESS] =
            bld.emit_uniformize(get_nir_src(load_offset));
      }

      while (loaded_dwords < total_dwords) {
         const unsigned block =
            choose_oword_block_size_dwords(devinfo,
                                           total_dwords - loaded_dwords);
         const unsigned block_bytes = block * 4;

         srcs[SURFACE_LOGICAL_SRC_IMM_ARG] = brw_imm_ud(block);

         const fs_builder &ubld = block <= 8 ? ubld8 : ubld16;
         ubld.emit(SHADER_OPCODE_UNALIGNED_OWORD_BLOCK_READ_LOGICAL,
                   retype(byte_offset(packed_consts, loaded_dwords * 4), BRW_REGISTER_TYPE_UD),
                   srcs, SURFACE_LOGICAL_NUM_SRCS)->size_written = align(block_bytes, REG_SIZE);

         loaded_dwords += block;

         ubld1.ADD(srcs[SURFACE_LOGICAL_SRC_ADDRESS],
                   srcs[SURFACE_LOGICAL_SRC_ADDRESS],
                   brw_imm_ud(block_bytes));
      }

      for (unsigned c = 0; c < instr->num_components; c++)
         bld.MOV(retype(offset(dest, bld, c), BRW_REGISTER_TYPE_UD),
                 component(packed_consts, c));

      break;
   }

   case nir_intrinsic_store_output: {
      assert(nir_src_bit_size(instr->src[0]) == 32);
      fs_reg src = get_nir_src(instr->src[0]);

      unsigned store_offset = nir_src_as_uint(instr->src[1]);
      unsigned num_components = instr->num_components;
      unsigned first_component = nir_intrinsic_component(instr);

      fs_reg new_dest = retype(offset(outputs[instr->const_index[0]], bld,
                                      4 * store_offset), src.type);
      for (unsigned j = 0; j < num_components; j++) {
         bld.MOV(offset(new_dest, bld, j + first_component),
                 offset(src, bld, j));
      }
      break;
   }

   case nir_intrinsic_ssbo_atomic:
   case nir_intrinsic_ssbo_atomic_swap:
      nir_emit_surface_atomic(bld, instr,
                              get_nir_buffer_intrinsic_index(bld, instr),
                              get_nir_src_bindless(instr->src[0]));
      break;

   case nir_intrinsic_get_ssbo_size: {
      assert(nir_src_num_components(instr->src[0]) == 1);

      /* A resinfo's sampler message is used to get the buffer size.  The
       * SIMD8's writeback message consists of four registers and SIMD16's
       * writeback message consists of 8 destination registers (two per each
       * component).  Because we are only interested on the first channel of
       * the first returned component, where resinfo returns the buffer size
       * for SURFTYPE_BUFFER, we can just use the SIMD8 variant regardless of
       * the dispatch width.
       */
      const fs_builder ubld = bld.exec_all().group(8, 0);
      fs_reg src_payload = ubld.vgrf(BRW_REGISTER_TYPE_UD);
      fs_reg ret_payload = ubld.vgrf(BRW_REGISTER_TYPE_UD, 4);

      /* Set LOD = 0 */
      ubld.MOV(src_payload, brw_imm_d(0));

      fs_reg srcs[GET_BUFFER_SIZE_SRCS];
      srcs[get_nir_src_bindless(instr->src[0]) ?
           GET_BUFFER_SIZE_SRC_SURFACE_HANDLE :
           GET_BUFFER_SIZE_SRC_SURFACE] =
         get_nir_buffer_intrinsic_index(bld, instr);
      srcs[GET_BUFFER_SIZE_SRC_LOD] = src_payload;
      fs_inst *inst = ubld.emit(SHADER_OPCODE_GET_BUFFER_SIZE, ret_payload,
                                srcs, GET_BUFFER_SIZE_SRCS);
      inst->header_size = 0;
      inst->mlen = 1;
      inst->size_written = 4 * REG_SIZE;

      /* SKL PRM, vol07, 3D Media GPGPU Engine, Bounds Checking and Faulting:
       *
       * "Out-of-bounds checking is always performed at a DWord granularity. If
       * any part of the DWord is out-of-bounds then the whole DWord is
       * considered out-of-bounds."
       *
       * This implies that types with size smaller than 4-bytes need to be
       * padded if they don't complete the last dword of the buffer. But as we
       * need to maintain the original size we need to reverse the padding
       * calculation to return the correct size to know the number of elements
       * of an unsized array. As we stored in the last two bits of the surface
       * size the needed padding for the buffer, we calculate here the
       * original buffer_size reversing the surface_size calculation:
       *
       * surface_size = isl_align(buffer_size, 4) +
       *                (isl_align(buffer_size) - buffer_size)
       *
       * buffer_size = surface_size & ~3 - surface_size & 3
       */

      fs_reg size_aligned4 = ubld.vgrf(BRW_REGISTER_TYPE_UD);
      fs_reg size_padding = ubld.vgrf(BRW_REGISTER_TYPE_UD);
      fs_reg buffer_size = ubld.vgrf(BRW_REGISTER_TYPE_UD);

      ubld.AND(size_padding, ret_payload, brw_imm_ud(3));
      ubld.AND(size_aligned4, ret_payload, brw_imm_ud(~3));
      ubld.ADD(buffer_size, size_aligned4, negate(size_padding));

      bld.MOV(retype(dest, ret_payload.type), component(buffer_size, 0));
      break;
   }

   case nir_intrinsic_load_scratch: {
      assert(devinfo->ver >= 7);

      assert(nir_dest_num_components(instr->dest) == 1);
      const unsigned bit_size = nir_dest_bit_size(instr->dest);
      fs_reg srcs[SURFACE_LOGICAL_NUM_SRCS];

      if (devinfo->verx10 >= 125) {
         const fs_builder ubld = bld.exec_all().group(1, 0);
         fs_reg handle = component(ubld.vgrf(BRW_REGISTER_TYPE_UD), 0);
         ubld.AND(handle, retype(brw_vec1_grf(0, 5), BRW_REGISTER_TYPE_UD),
                          brw_imm_ud(INTEL_MASK(31, 10)));
         srcs[SURFACE_LOGICAL_SRC_SURFACE] = brw_imm_ud(GFX125_NON_BINDLESS);
         srcs[SURFACE_LOGICAL_SRC_SURFACE_HANDLE] = handle;
      } else if (devinfo->ver >= 8) {
         srcs[SURFACE_LOGICAL_SRC_SURFACE] =
            brw_imm_ud(GFX8_BTI_STATELESS_NON_COHERENT);
      } else {
         srcs[SURFACE_LOGICAL_SRC_SURFACE] = brw_imm_ud(BRW_BTI_STATELESS);
      }

      srcs[SURFACE_LOGICAL_SRC_IMM_DIMS] = brw_imm_ud(1);
      srcs[SURFACE_LOGICAL_SRC_IMM_ARG] = brw_imm_ud(bit_size);
      srcs[SURFACE_LOGICAL_SRC_ALLOW_SAMPLE_MASK] = brw_imm_ud(0);
      const fs_reg nir_addr = get_nir_src(instr->src[0]);

      /* Make dest unsigned because that's what the temporary will be */
      dest.type = brw_reg_type_from_bit_size(bit_size, BRW_REGISTER_TYPE_UD);

      /* Read the vector */
      assert(nir_dest_num_components(instr->dest) == 1);
      assert(bit_size <= 32);
      assert(nir_intrinsic_align(instr) > 0);
      if (bit_size == 32 &&
          nir_intrinsic_align(instr) >= 4) {
         if (devinfo->verx10 >= 125) {
            assert(bit_size == 32 &&
                   nir_intrinsic_align(instr) >= 4);

            srcs[SURFACE_LOGICAL_SRC_ADDRESS] =
               swizzle_nir_scratch_addr(bld, nir_addr, false);
            srcs[SURFACE_LOGICAL_SRC_IMM_ARG] = brw_imm_ud(1);

            bld.emit(SHADER_OPCODE_UNTYPED_SURFACE_READ_LOGICAL,
                     dest, srcs, SURFACE_LOGICAL_NUM_SRCS);
         } else {
            /* The offset for a DWORD scattered message is in dwords. */
            srcs[SURFACE_LOGICAL_SRC_ADDRESS] =
               swizzle_nir_scratch_addr(bld, nir_addr, true);

            bld.emit(SHADER_OPCODE_DWORD_SCATTERED_READ_LOGICAL,
                     dest, srcs, SURFACE_LOGICAL_NUM_SRCS);
         }
      } else {
         srcs[SURFACE_LOGICAL_SRC_ADDRESS] =
            swizzle_nir_scratch_addr(bld, nir_addr, false);

         fs_reg read_result = bld.vgrf(BRW_REGISTER_TYPE_UD);
         bld.emit(SHADER_OPCODE_BYTE_SCATTERED_READ_LOGICAL,
                  read_result, srcs, SURFACE_LOGICAL_NUM_SRCS);
         bld.MOV(dest, read_result);
      }

      shader_stats.fill_count += DIV_ROUND_UP(dispatch_width, 16);
      break;
   }

   case nir_intrinsic_store_scratch: {
      assert(devinfo->ver >= 7);

      assert(nir_src_num_components(instr->src[0]) == 1);
      const unsigned bit_size = nir_src_bit_size(instr->src[0]);
      fs_reg srcs[SURFACE_LOGICAL_NUM_SRCS];

      if (devinfo->verx10 >= 125) {
         const fs_builder ubld = bld.exec_all().group(1, 0);
         fs_reg handle = component(ubld.vgrf(BRW_REGISTER_TYPE_UD), 0);
         ubld.AND(handle, retype(brw_vec1_grf(0, 5), BRW_REGISTER_TYPE_UD),
                          brw_imm_ud(INTEL_MASK(31, 10)));
         srcs[SURFACE_LOGICAL_SRC_SURFACE] = brw_imm_ud(GFX125_NON_BINDLESS);
         srcs[SURFACE_LOGICAL_SRC_SURFACE_HANDLE] = handle;
      } else if (devinfo->ver >= 8) {
         srcs[SURFACE_LOGICAL_SRC_SURFACE] =
            brw_imm_ud(GFX8_BTI_STATELESS_NON_COHERENT);
      } else {
         srcs[SURFACE_LOGICAL_SRC_SURFACE] = brw_imm_ud(BRW_BTI_STATELESS);
      }

      srcs[SURFACE_LOGICAL_SRC_IMM_DIMS] = brw_imm_ud(1);
      srcs[SURFACE_LOGICAL_SRC_IMM_ARG] = brw_imm_ud(bit_size);
      /**
       * While this instruction has side-effects, it should not be predicated
       * on sample mask, because otherwise fs helper invocations would
       * load undefined values from scratch memory. And scratch memory
       * load-stores are produced from operations without side-effects, thus
       * they should not have different behaviour in the helper invocations.
       */
      srcs[SURFACE_LOGICAL_SRC_ALLOW_SAMPLE_MASK] = brw_imm_ud(0);
      const fs_reg nir_addr = get_nir_src(instr->src[1]);

      fs_reg data = get_nir_src(instr->src[0]);
      data.type = brw_reg_type_from_bit_size(bit_size, BRW_REGISTER_TYPE_UD);

      assert(nir_src_num_components(instr->src[0]) == 1);
      assert(bit_size <= 32);
      assert(nir_intrinsic_write_mask(instr) == 1);
      assert(nir_intrinsic_align(instr) > 0);
      if (bit_size == 32 &&
          nir_intrinsic_align(instr) >= 4) {
         if (devinfo->verx10 >= 125) {
            srcs[SURFACE_LOGICAL_SRC_DATA] = data;

            srcs[SURFACE_LOGICAL_SRC_ADDRESS] =
               swizzle_nir_scratch_addr(bld, nir_addr, false);
            srcs[SURFACE_LOGICAL_SRC_IMM_ARG] = brw_imm_ud(1);

            bld.emit(SHADER_OPCODE_UNTYPED_SURFACE_WRITE_LOGICAL,
                     dest, srcs, SURFACE_LOGICAL_NUM_SRCS);
         } else {
            srcs[SURFACE_LOGICAL_SRC_DATA] = data;

            /* The offset for a DWORD scattered message is in dwords. */
            srcs[SURFACE_LOGICAL_SRC_ADDRESS] =
               swizzle_nir_scratch_addr(bld, nir_addr, true);

            bld.emit(SHADER_OPCODE_DWORD_SCATTERED_WRITE_LOGICAL,
                     fs_reg(), srcs, SURFACE_LOGICAL_NUM_SRCS);
         }
      } else {
         srcs[SURFACE_LOGICAL_SRC_DATA] = bld.vgrf(BRW_REGISTER_TYPE_UD);
         bld.MOV(srcs[SURFACE_LOGICAL_SRC_DATA], data);

         srcs[SURFACE_LOGICAL_SRC_ADDRESS] =
            swizzle_nir_scratch_addr(bld, nir_addr, false);

         bld.emit(SHADER_OPCODE_BYTE_SCATTERED_WRITE_LOGICAL,
                  fs_reg(), srcs, SURFACE_LOGICAL_NUM_SRCS);
      }
      shader_stats.spill_count += DIV_ROUND_UP(dispatch_width, 16);
      break;
   }

   case nir_intrinsic_load_subgroup_size:
      /* This should only happen for fragment shaders because every other case
       * is lowered in NIR so we can optimize on it.
       */
      assert(stage == MESA_SHADER_FRAGMENT);
      bld.MOV(retype(dest, BRW_REGISTER_TYPE_D), brw_imm_d(dispatch_width));
      break;

   case nir_intrinsic_load_subgroup_invocation:
      bld.MOV(retype(dest, BRW_REGISTER_TYPE_D),
              nir_system_values[SYSTEM_VALUE_SUBGROUP_INVOCATION]);
      break;

   case nir_intrinsic_load_subgroup_eq_mask:
   case nir_intrinsic_load_subgroup_ge_mask:
   case nir_intrinsic_load_subgroup_gt_mask:
   case nir_intrinsic_load_subgroup_le_mask:
   case nir_intrinsic_load_subgroup_lt_mask:
      unreachable("not reached");

   case nir_intrinsic_vote_any: {
      const fs_builder ubld = bld.exec_all().group(1, 0);

      /* The any/all predicates do not consider channel enables. To prevent
       * dead channels from affecting the result, we initialize the flag with
       * with the identity value for the logical operation.
       */
      if (dispatch_width == 32) {
         /* For SIMD32, we use a UD type so we fill both f0.0 and f0.1. */
         ubld.MOV(retype(brw_flag_reg(0, 0), BRW_REGISTER_TYPE_UD),
                         brw_imm_ud(0));
      } else {
         ubld.MOV(brw_flag_reg(0, 0), brw_imm_uw(0));
      }
      bld.CMP(bld.null_reg_d(), get_nir_src(instr->src[0]), brw_imm_d(0), BRW_CONDITIONAL_NZ);

      /* For some reason, the any/all predicates don't work properly with
       * SIMD32.  In particular, it appears that a SEL with a QtrCtrl of 2H
       * doesn't read the correct subset of the flag register and you end up
       * getting garbage in the second half.  Work around this by using a pair
       * of 1-wide MOVs and scattering the result.
       */
      fs_reg res1 = ubld.vgrf(BRW_REGISTER_TYPE_D);
      ubld.MOV(res1, brw_imm_d(0));
      set_predicate(dispatch_width == 8  ? BRW_PREDICATE_ALIGN1_ANY8H :
                    dispatch_width == 16 ? BRW_PREDICATE_ALIGN1_ANY16H :
                                           BRW_PREDICATE_ALIGN1_ANY32H,
                    ubld.MOV(res1, brw_imm_d(-1)));

      bld.MOV(retype(dest, BRW_REGISTER_TYPE_D), component(res1, 0));
      break;
   }
   case nir_intrinsic_vote_all: {
      const fs_builder ubld = bld.exec_all().group(1, 0);

      /* The any/all predicates do not consider channel enables. To prevent
       * dead channels from affecting the result, we initialize the flag with
       * with the identity value for the logical operation.
       */
      if (dispatch_width == 32) {
         /* For SIMD32, we use a UD type so we fill both f0.0 and f0.1. */
         ubld.MOV(retype(brw_flag_reg(0, 0), BRW_REGISTER_TYPE_UD),
                         brw_imm_ud(0xffffffff));
      } else {
         ubld.MOV(brw_flag_reg(0, 0), brw_imm_uw(0xffff));
      }
      bld.CMP(bld.null_reg_d(), get_nir_src(instr->src[0]), brw_imm_d(0), BRW_CONDITIONAL_NZ);

      /* For some reason, the any/all predicates don't work properly with
       * SIMD32.  In particular, it appears that a SEL with a QtrCtrl of 2H
       * doesn't read the correct subset of the flag register and you end up
       * getting garbage in the second half.  Work around this by using a pair
       * of 1-wide MOVs and scattering the result.
       */
      fs_reg res1 = ubld.vgrf(BRW_REGISTER_TYPE_D);
      ubld.MOV(res1, brw_imm_d(0));
      set_predicate(dispatch_width == 8  ? BRW_PREDICATE_ALIGN1_ALL8H :
                    dispatch_width == 16 ? BRW_PREDICATE_ALIGN1_ALL16H :
                                           BRW_PREDICATE_ALIGN1_ALL32H,
                    ubld.MOV(res1, brw_imm_d(-1)));

      bld.MOV(retype(dest, BRW_REGISTER_TYPE_D), component(res1, 0));
      break;
   }
   case nir_intrinsic_vote_feq:
   case nir_intrinsic_vote_ieq: {
      fs_reg value = get_nir_src(instr->src[0]);
      if (instr->intrinsic == nir_intrinsic_vote_feq) {
         const unsigned bit_size = nir_src_bit_size(instr->src[0]);
         value.type = bit_size == 8 ? BRW_REGISTER_TYPE_B :
            brw_reg_type_from_bit_size(bit_size, BRW_REGISTER_TYPE_F);
      }

      fs_reg uniformized = bld.emit_uniformize(value);
      const fs_builder ubld = bld.exec_all().group(1, 0);

      /* The any/all predicates do not consider channel enables. To prevent
       * dead channels from affecting the result, we initialize the flag with
       * with the identity value for the logical operation.
       */
      if (dispatch_width == 32) {
         /* For SIMD32, we use a UD type so we fill both f0.0 and f0.1. */
         ubld.MOV(retype(brw_flag_reg(0, 0), BRW_REGISTER_TYPE_UD),
                         brw_imm_ud(0xffffffff));
      } else {
         ubld.MOV(brw_flag_reg(0, 0), brw_imm_uw(0xffff));
      }
      bld.CMP(bld.null_reg_d(), value, uniformized, BRW_CONDITIONAL_Z);

      /* For some reason, the any/all predicates don't work properly with
       * SIMD32.  In particular, it appears that a SEL with a QtrCtrl of 2H
       * doesn't read the correct subset of the flag register and you end up
       * getting garbage in the second half.  Work around this by using a pair
       * of 1-wide MOVs and scattering the result.
       */
      fs_reg res1 = ubld.vgrf(BRW_REGISTER_TYPE_D);
      ubld.MOV(res1, brw_imm_d(0));
      set_predicate(dispatch_width == 8  ? BRW_PREDICATE_ALIGN1_ALL8H :
                    dispatch_width == 16 ? BRW_PREDICATE_ALIGN1_ALL16H :
                                           BRW_PREDICATE_ALIGN1_ALL32H,
                    ubld.MOV(res1, brw_imm_d(-1)));

      bld.MOV(retype(dest, BRW_REGISTER_TYPE_D), component(res1, 0));
      break;
   }

   case nir_intrinsic_ballot: {
      const fs_reg value = retype(get_nir_src(instr->src[0]),
                                  BRW_REGISTER_TYPE_UD);
      struct brw_reg flag = brw_flag_reg(0, 0);
      /* FIXME: For SIMD32 programs, this causes us to stomp on f0.1 as well
       * as f0.0.  This is a problem for fragment programs as we currently use
       * f0.1 for discards.  Fortunately, we don't support SIMD32 fragment
       * programs yet so this isn't a problem.  When we do, something will
       * have to change.
       */
      if (dispatch_width == 32)
         flag.type = BRW_REGISTER_TYPE_UD;

      bld.exec_all().group(1, 0).MOV(flag, brw_imm_ud(0u));
      bld.CMP(bld.null_reg_ud(), value, brw_imm_ud(0u), BRW_CONDITIONAL_NZ);

      if (nir_dest_bit_size(instr->dest) > 32) {
         dest.type = BRW_REGISTER_TYPE_UQ;
      } else {
         dest.type = BRW_REGISTER_TYPE_UD;
      }
      bld.MOV(dest, flag);
      break;
   }

   case nir_intrinsic_read_invocation: {
      const fs_reg value = get_nir_src(instr->src[0]);
      const fs_reg invocation = get_nir_src(instr->src[1]);

      fs_reg tmp = bld.vgrf(value.type);

      /* When for some reason the subgroup_size picked by NIR is larger than
       * the dispatch size picked by the backend (this could happen in RT,
       * FS), bound the invocation to the dispatch size.
       */
      fs_reg bound_invocation;
      if (api_subgroup_size == 0 ||
          bld.dispatch_width() < api_subgroup_size) {
         bound_invocation = bld.vgrf(BRW_REGISTER_TYPE_UD);
         bld.AND(bound_invocation, invocation, brw_imm_ud(dispatch_width - 1));
      } else {
         bound_invocation = invocation;
      }
      bld.exec_all().emit(SHADER_OPCODE_BROADCAST, tmp, value,
                          bld.emit_uniformize(bound_invocation));

      bld.MOV(retype(dest, value.type), fs_reg(component(tmp, 0)));
      break;
   }

   case nir_intrinsic_read_first_invocation: {
      const fs_reg value = get_nir_src(instr->src[0]);
      bld.MOV(retype(dest, value.type), bld.emit_uniformize(value));
      break;
   }

   case nir_intrinsic_shuffle: {
      const fs_reg value = get_nir_src(instr->src[0]);
      const fs_reg index = get_nir_src(instr->src[1]);

      bld.emit(SHADER_OPCODE_SHUFFLE, retype(dest, value.type), value, index);
      break;
   }

   case nir_intrinsic_first_invocation: {
      fs_reg tmp = bld.vgrf(BRW_REGISTER_TYPE_UD);
      bld.exec_all().emit(SHADER_OPCODE_FIND_LIVE_CHANNEL, tmp);
      bld.MOV(retype(dest, BRW_REGISTER_TYPE_UD),
              fs_reg(component(tmp, 0)));
      break;
   }

   case nir_intrinsic_last_invocation: {
      fs_reg tmp = bld.vgrf(BRW_REGISTER_TYPE_UD);
      bld.exec_all().emit(SHADER_OPCODE_FIND_LAST_LIVE_CHANNEL, tmp);
      bld.MOV(retype(dest, BRW_REGISTER_TYPE_UD),
              fs_reg(component(tmp, 0)));
      break;
   }

   case nir_intrinsic_quad_broadcast: {
      const fs_reg value = get_nir_src(instr->src[0]);
      const unsigned index = nir_src_as_uint(instr->src[1]);

      bld.emit(SHADER_OPCODE_CLUSTER_BROADCAST, retype(dest, value.type),
               value, brw_imm_ud(index), brw_imm_ud(4));
      break;
   }

   case nir_intrinsic_quad_swap_horizontal: {
      const fs_reg value = get_nir_src(instr->src[0]);
      const fs_reg tmp = bld.vgrf(value.type);
      if (devinfo->ver <= 7) {
         /* The hardware doesn't seem to support these crazy regions with
          * compressed instructions on gfx7 and earlier so we fall back to
          * using quad swizzles.  Fortunately, we don't support 64-bit
          * anything in Vulkan on gfx7.
          */
         assert(nir_src_bit_size(instr->src[0]) == 32);
         const fs_builder ubld = bld.exec_all();
         ubld.emit(SHADER_OPCODE_QUAD_SWIZZLE, tmp, value,
                   brw_imm_ud(BRW_SWIZZLE4(1,0,3,2)));
         bld.MOV(retype(dest, value.type), tmp);
      } else {
         const fs_builder ubld = bld.exec_all().group(dispatch_width / 2, 0);

         const fs_reg src_left = horiz_stride(value, 2);
         const fs_reg src_right = horiz_stride(horiz_offset(value, 1), 2);
         const fs_reg tmp_left = horiz_stride(tmp, 2);
         const fs_reg tmp_right = horiz_stride(horiz_offset(tmp, 1), 2);

         ubld.MOV(tmp_left, src_right);
         ubld.MOV(tmp_right, src_left);

      }
      bld.MOV(retype(dest, value.type), tmp);
      break;
   }

   case nir_intrinsic_quad_swap_vertical: {
      const fs_reg value = get_nir_src(instr->src[0]);
      if (nir_src_bit_size(instr->src[0]) == 32) {
         /* For 32-bit, we can use a SIMD4x2 instruction to do this easily */
         const fs_reg tmp = bld.vgrf(value.type);
         const fs_builder ubld = bld.exec_all();
         ubld.emit(SHADER_OPCODE_QUAD_SWIZZLE, tmp, value,
                   brw_imm_ud(BRW_SWIZZLE4(2,3,0,1)));
         bld.MOV(retype(dest, value.type), tmp);
      } else {
         /* For larger data types, we have to either emit dispatch_width many
          * MOVs or else fall back to doing indirects.
          */
         fs_reg idx = bld.vgrf(BRW_REGISTER_TYPE_W);
         bld.XOR(idx, nir_system_values[SYSTEM_VALUE_SUBGROUP_INVOCATION],
                      brw_imm_w(0x2));
         bld.emit(SHADER_OPCODE_SHUFFLE, retype(dest, value.type), value, idx);
      }
      break;
   }

   case nir_intrinsic_quad_swap_diagonal: {
      const fs_reg value = get_nir_src(instr->src[0]);
      if (nir_src_bit_size(instr->src[0]) == 32) {
         /* For 32-bit, we can use a SIMD4x2 instruction to do this easily */
         const fs_reg tmp = bld.vgrf(value.type);
         const fs_builder ubld = bld.exec_all();
         ubld.emit(SHADER_OPCODE_QUAD_SWIZZLE, tmp, value,
                   brw_imm_ud(BRW_SWIZZLE4(3,2,1,0)));
         bld.MOV(retype(dest, value.type), tmp);
      } else {
         /* For larger data types, we have to either emit dispatch_width many
          * MOVs or else fall back to doing indirects.
          */
         fs_reg idx = bld.vgrf(BRW_REGISTER_TYPE_W);
         bld.XOR(idx, nir_system_values[SYSTEM_VALUE_SUBGROUP_INVOCATION],
                      brw_imm_w(0x3));
         bld.emit(SHADER_OPCODE_SHUFFLE, retype(dest, value.type), value, idx);
      }
      break;
   }

   case nir_intrinsic_reduce: {
      fs_reg src = get_nir_src(instr->src[0]);
      nir_op redop = (nir_op)nir_intrinsic_reduction_op(instr);
      unsigned cluster_size = nir_intrinsic_cluster_size(instr);
      if (cluster_size == 0 || cluster_size > dispatch_width)
         cluster_size = dispatch_width;

      /* Figure out the source type */
      src.type = brw_type_for_nir_type(devinfo,
         (nir_alu_type)(nir_op_infos[redop].input_types[0] |
                        nir_src_bit_size(instr->src[0])));

      fs_reg identity = brw_nir_reduction_op_identity(bld, redop, src.type);
      opcode brw_op = brw_op_for_nir_reduction_op(redop);
      brw_conditional_mod cond_mod = brw_cond_mod_for_nir_reduction_op(redop);

      /* Set up a register for all of our scratching around and initialize it
       * to reduction operation's identity value.
       */
      fs_reg scan = bld.vgrf(src.type);
      bld.exec_all().emit(SHADER_OPCODE_SEL_EXEC, scan, src, identity);

      bld.emit_scan(brw_op, scan, cluster_size, cond_mod);

      dest.type = src.type;
      if (cluster_size * type_sz(src.type) >= REG_SIZE * 2) {
         /* In this case, CLUSTER_BROADCAST instruction isn't needed because
          * the distance between clusters is at least 2 GRFs.  In this case,
          * we don't need the weird striding of the CLUSTER_BROADCAST
          * instruction and can just do regular MOVs.
          */
         assert((cluster_size * type_sz(src.type)) % (REG_SIZE * 2) == 0);
         const unsigned groups =
            (dispatch_width * type_sz(src.type)) / (REG_SIZE * 2);
         const unsigned group_size = dispatch_width / groups;
         for (unsigned i = 0; i < groups; i++) {
            const unsigned cluster = (i * group_size) / cluster_size;
            const unsigned comp = cluster * cluster_size + (cluster_size - 1);
            bld.group(group_size, i).MOV(horiz_offset(dest, i * group_size),
                                         component(scan, comp));
         }
      } else {
         bld.emit(SHADER_OPCODE_CLUSTER_BROADCAST, dest, scan,
                  brw_imm_ud(cluster_size - 1), brw_imm_ud(cluster_size));
      }
      break;
   }

   case nir_intrinsic_inclusive_scan:
   case nir_intrinsic_exclusive_scan: {
      fs_reg src = get_nir_src(instr->src[0]);
      nir_op redop = (nir_op)nir_intrinsic_reduction_op(instr);

      /* Figure out the source type */
      src.type = brw_type_for_nir_type(devinfo,
         (nir_alu_type)(nir_op_infos[redop].input_types[0] |
                        nir_src_bit_size(instr->src[0])));

      fs_reg identity = brw_nir_reduction_op_identity(bld, redop, src.type);
      opcode brw_op = brw_op_for_nir_reduction_op(redop);
      brw_conditional_mod cond_mod = brw_cond_mod_for_nir_reduction_op(redop);

      /* Set up a register for all of our scratching around and initialize it
       * to reduction operation's identity value.
       */
      fs_reg scan = bld.vgrf(src.type);
      const fs_builder allbld = bld.exec_all();
      allbld.emit(SHADER_OPCODE_SEL_EXEC, scan, src, identity);

      if (instr->intrinsic == nir_intrinsic_exclusive_scan) {
         /* Exclusive scan is a bit harder because we have to do an annoying
          * shift of the contents before we can begin.  To make things worse,
          * we can't do this with a normal stride; we have to use indirects.
          */
         fs_reg shifted = bld.vgrf(src.type);
         fs_reg idx = bld.vgrf(BRW_REGISTER_TYPE_W);
         allbld.ADD(idx, nir_system_values[SYSTEM_VALUE_SUBGROUP_INVOCATION],
                         brw_imm_w(-1));
         allbld.emit(SHADER_OPCODE_SHUFFLE, shifted, scan, idx);
         allbld.group(1, 0).MOV(component(shifted, 0), identity);
         scan = shifted;
      }

      bld.emit_scan(brw_op, scan, dispatch_width, cond_mod);

      bld.MOV(retype(dest, src.type), scan);
      break;
   }

   case nir_intrinsic_load_global_block_intel: {
      assert(nir_dest_bit_size(instr->dest) == 32);

      fs_reg address = bld.emit_uniformize(get_nir_src(instr->src[0]));

      const fs_builder ubld1 = bld.exec_all().group(1, 0);
      const fs_builder ubld8 = bld.exec_all().group(8, 0);
      const fs_builder ubld16 = bld.exec_all().group(16, 0);

      const unsigned total = instr->num_components * dispatch_width;
      unsigned loaded = 0;

      while (loaded < total) {
         const unsigned block =
            choose_oword_block_size_dwords(devinfo, total - loaded);
         const unsigned block_bytes = block * 4;

         const fs_builder &ubld = block == 8 ? ubld8 : ubld16;

         fs_reg srcs[A64_LOGICAL_NUM_SRCS];
         srcs[A64_LOGICAL_ADDRESS] = address;
         srcs[A64_LOGICAL_SRC] = fs_reg(); /* No source data */
         srcs[A64_LOGICAL_ARG] = brw_imm_ud(block);
         srcs[A64_LOGICAL_ENABLE_HELPERS] = brw_imm_ud(1);
         ubld.emit(SHADER_OPCODE_A64_UNALIGNED_OWORD_BLOCK_READ_LOGICAL,
                   retype(byte_offset(dest, loaded * 4), BRW_REGISTER_TYPE_UD),
                   srcs, A64_LOGICAL_NUM_SRCS)->size_written = block_bytes;

         increment_a64_address(ubld1, address, block_bytes);
         loaded += block;
      }

      assert(loaded == total);
      break;
   }

   case nir_intrinsic_store_global_block_intel: {
      assert(nir_src_bit_size(instr->src[0]) == 32);

      fs_reg address = bld.emit_uniformize(get_nir_src(instr->src[1]));
      fs_reg src = get_nir_src(instr->src[0]);

      const fs_builder ubld1 = bld.exec_all().group(1, 0);
      const fs_builder ubld8 = bld.exec_all().group(8, 0);
      const fs_builder ubld16 = bld.exec_all().group(16, 0);

      const unsigned total = instr->num_components * dispatch_width;
      unsigned written = 0;

      while (written < total) {
         const unsigned block =
            choose_oword_block_size_dwords(devinfo, total - written);

         fs_reg srcs[A64_LOGICAL_NUM_SRCS];
         srcs[A64_LOGICAL_ADDRESS] = address;
         srcs[A64_LOGICAL_SRC] = retype(byte_offset(src, written * 4),
                                        BRW_REGISTER_TYPE_UD);
         srcs[A64_LOGICAL_ARG] = brw_imm_ud(block);
         srcs[A64_LOGICAL_ENABLE_HELPERS] = brw_imm_ud(0);

         const fs_builder &ubld = block == 8 ? ubld8 : ubld16;
         ubld.emit(SHADER_OPCODE_A64_OWORD_BLOCK_WRITE_LOGICAL, fs_reg(),
                   srcs, A64_LOGICAL_NUM_SRCS);

         const unsigned block_bytes = block * 4;
         increment_a64_address(ubld1, address, block_bytes);
         written += block;
      }

      assert(written == total);
      break;
   }

   case nir_intrinsic_load_shared_block_intel:
   case nir_intrinsic_load_ssbo_block_intel: {
      assert(nir_dest_bit_size(instr->dest) == 32);

      const bool is_ssbo =
         instr->intrinsic == nir_intrinsic_load_ssbo_block_intel;
      fs_reg address = bld.emit_uniformize(get_nir_src(instr->src[is_ssbo ? 1 : 0]));

      fs_reg srcs[SURFACE_LOGICAL_NUM_SRCS];
      srcs[SURFACE_LOGICAL_SRC_SURFACE] = is_ssbo ?
         get_nir_buffer_intrinsic_index(bld, instr) :
         fs_reg(brw_imm_ud(GFX7_BTI_SLM));
      srcs[SURFACE_LOGICAL_SRC_ADDRESS] = address;

      const fs_builder ubld1 = bld.exec_all().group(1, 0);
      const fs_builder ubld8 = bld.exec_all().group(8, 0);
      const fs_builder ubld16 = bld.exec_all().group(16, 0);

      const unsigned total = instr->num_components * dispatch_width;
      unsigned loaded = 0;

      while (loaded < total) {
         const unsigned block =
            choose_oword_block_size_dwords(devinfo, total - loaded);
         const unsigned block_bytes = block * 4;

         srcs[SURFACE_LOGICAL_SRC_IMM_ARG] = brw_imm_ud(block);

         const fs_builder &ubld = block == 8 ? ubld8 : ubld16;
         ubld.emit(SHADER_OPCODE_UNALIGNED_OWORD_BLOCK_READ_LOGICAL,
                   retype(byte_offset(dest, loaded * 4), BRW_REGISTER_TYPE_UD),
                   srcs, SURFACE_LOGICAL_NUM_SRCS)->size_written = block_bytes;

         ubld1.ADD(address, address, brw_imm_ud(block_bytes));
         loaded += block;
      }

      assert(loaded == total);
      break;
   }

   case nir_intrinsic_store_shared_block_intel:
   case nir_intrinsic_store_ssbo_block_intel: {
      assert(nir_src_bit_size(instr->src[0]) == 32);

      const bool is_ssbo =
         instr->intrinsic == nir_intrinsic_store_ssbo_block_intel;

      fs_reg address = bld.emit_uniformize(get_nir_src(instr->src[is_ssbo ? 2 : 1]));
      fs_reg src = get_nir_src(instr->src[0]);

      fs_reg srcs[SURFACE_LOGICAL_NUM_SRCS];
      srcs[SURFACE_LOGICAL_SRC_SURFACE] = is_ssbo ?
         get_nir_buffer_intrinsic_index(bld, instr) :
         fs_reg(brw_imm_ud(GFX7_BTI_SLM));
      srcs[SURFACE_LOGICAL_SRC_ADDRESS] = address;

      const fs_builder ubld1 = bld.exec_all().group(1, 0);
      const fs_builder ubld8 = bld.exec_all().group(8, 0);
      const fs_builder ubld16 = bld.exec_all().group(16, 0);

      const unsigned total = instr->num_components * dispatch_width;
      unsigned written = 0;

      while (written < total) {
         const unsigned block =
            choose_oword_block_size_dwords(devinfo, total - written);

         srcs[SURFACE_LOGICAL_SRC_IMM_ARG] = brw_imm_ud(block);
         srcs[SURFACE_LOGICAL_SRC_DATA] =
            retype(byte_offset(src, written * 4), BRW_REGISTER_TYPE_UD);

         const fs_builder &ubld = block == 8 ? ubld8 : ubld16;
         ubld.emit(SHADER_OPCODE_OWORD_BLOCK_WRITE_LOGICAL,
                   fs_reg(), srcs, SURFACE_LOGICAL_NUM_SRCS);

         const unsigned block_bytes = block * 4;
         ubld1.ADD(address, address, brw_imm_ud(block_bytes));
         written += block;
      }

      assert(written == total);
      break;
   }

   case nir_intrinsic_load_topology_id_intel: {
       /* These move around basically every hardware generation, so don'
        * do any >= checks and fail if the platform hasn't explicitly
        * been enabled here.
        */
      assert(devinfo->ver == 12);

      /* Here is what the layout of SR0 looks like on Gfx12 :
       *   [13:11] : Slice ID.
       *   [10:9]  : Dual-SubSlice ID
       *   [8]     : SubSlice ID
       *   [7]     : EUID[2] (aka EU Row ID)
       *   [6]     : Reserved
       *   [5:4]   : EUID[1:0]
       *   [2:0]   : Thread ID
       */
      fs_reg raw_id = bld.vgrf(BRW_REGISTER_TYPE_UD);
      bld.emit(SHADER_OPCODE_READ_SR_REG, raw_id, brw_imm_ud(0));
      switch (nir_intrinsic_base(instr)) {
      case BRW_TOPOLOGY_ID_DSS:
         bld.AND(raw_id, raw_id, brw_imm_ud(0x3fff));
         /* Get rid of anything below dualsubslice */
         bld.SHR(retype(dest, BRW_REGISTER_TYPE_UD), raw_id, brw_imm_ud(9));
         break;
      case BRW_TOPOLOGY_ID_EU_THREAD_SIMD: {
         limit_dispatch_width(16, "Topology helper for Ray queries, "
                              "not supported in SIMD32 mode.");
         fs_reg dst = retype(dest, BRW_REGISTER_TYPE_UD);

         /* EU[3:0] << 7
          *
          * The 4bit EU[3:0] we need to build for ray query memory addresses
          * computations is a bit odd :
          *
          *   EU[1:0] = raw_id[5:4] (identified as EUID[1:0])
          *   EU[2]   = raw_id[8]   (identified as SubSlice ID)
          *   EU[3]   = raw_id[7]   (identified as EUID[2] or Row ID)
          */
         {
            fs_reg tmp = bld.vgrf(BRW_REGISTER_TYPE_UD);
            bld.AND(tmp, raw_id, brw_imm_ud(INTEL_MASK(7, 7)));
            bld.SHL(dst, tmp, brw_imm_ud(3));
            bld.AND(tmp, raw_id, brw_imm_ud(INTEL_MASK(8, 8)));
            bld.SHL(tmp, tmp, brw_imm_ud(1));
            bld.OR(dst, dst, tmp);
            bld.AND(tmp, raw_id, brw_imm_ud(INTEL_MASK(5, 4)));
            bld.SHL(tmp, tmp, brw_imm_ud(3));
            bld.OR(dst, dst, tmp);
         }

         /* ThreadID[2:0] << 4 (ThreadID comes from raw_id[2:0]) */
         {
            bld.AND(raw_id, raw_id, brw_imm_ud(INTEL_MASK(2, 0)));
            bld.SHL(raw_id, raw_id, brw_imm_ud(4));
            bld.OR(dst, dst, raw_id);
         }

         /* LaneID[0:3] << 0 (We build up LaneID by putting the right number
          *                   in each lane)
          */
         fs_reg tmp = bld.vgrf(BRW_REGISTER_TYPE_UW);
         const fs_builder ubld8 = bld.exec_all().group(8, 0);
         ubld8.MOV(quarter(tmp, 0), brw_imm_v(0x76543210));
         if (bld.dispatch_width() == 16) {
            /* Sets 0xfedcba98 to the upper part of the register. */
            ubld8.ADD(quarter(tmp, 1), quarter(tmp, 0), brw_imm_ud(8));
         }
         bld.ADD(dst, dst, tmp);
         break;
      }
      default:
         unreachable("Invalid topology id type");
      }
      break;
   }

   case nir_intrinsic_load_btd_stack_id_intel:
      if (stage == MESA_SHADER_COMPUTE) {
         assert(brw_cs_prog_data(prog_data)->uses_btd_stack_ids);
      } else {
         assert(brw_shader_stage_is_bindless(stage));
      }
      /* Stack IDs are always in R1 regardless of whether we're coming from a
       * bindless shader or a regular compute shader.
       */
      bld.MOV(retype(dest, BRW_REGISTER_TYPE_UD),
              retype(brw_vec8_grf(1, 0), BRW_REGISTER_TYPE_UW));
      break;

   case nir_intrinsic_btd_spawn_intel:
      if (stage == MESA_SHADER_COMPUTE) {
         assert(brw_cs_prog_data(prog_data)->uses_btd_stack_ids);
      } else {
         assert(brw_shader_stage_is_bindless(stage));
      }
      /* Make sure all the pointers to resume shaders have landed where other
       * threads can see them.
       */
      emit_rt_lsc_fence(bld, LSC_FENCE_LOCAL, LSC_FLUSH_TYPE_NONE);

      bld.emit(SHADER_OPCODE_BTD_SPAWN_LOGICAL, bld.null_reg_ud(),
               bld.emit_uniformize(get_nir_src(instr->src[0])),
               get_nir_src(instr->src[1]));
      break;

   case nir_intrinsic_btd_retire_intel:
      if (stage == MESA_SHADER_COMPUTE) {
         assert(brw_cs_prog_data(prog_data)->uses_btd_stack_ids);
      } else {
         assert(brw_shader_stage_is_bindless(stage));
      }
      /* Make sure all the pointers to resume shaders have landed where other
       * threads can see them.
       */
      emit_rt_lsc_fence(bld, LSC_FENCE_LOCAL, LSC_FLUSH_TYPE_NONE);
      bld.emit(SHADER_OPCODE_BTD_RETIRE_LOGICAL);
      break;

   case nir_intrinsic_trace_ray_intel: {
      const bool synchronous = nir_intrinsic_synchronous(instr);
      assert(brw_shader_stage_is_bindless(stage) || synchronous);

      /* Make sure all the previous RT structure writes are visible to the RT
       * fixed function within the DSS, as well as stack pointers to resume
       * shaders.
       */
      emit_rt_lsc_fence(bld, LSC_FENCE_LOCAL, LSC_FLUSH_TYPE_NONE);

      fs_reg srcs[RT_LOGICAL_NUM_SRCS];

      fs_reg globals = get_nir_src(instr->src[0]);
      srcs[RT_LOGICAL_SRC_GLOBALS] = bld.emit_uniformize(globals);
      srcs[RT_LOGICAL_SRC_BVH_LEVEL] = get_nir_src(instr->src[1]);
      srcs[RT_LOGICAL_SRC_TRACE_RAY_CONTROL] = get_nir_src(instr->src[2]);
      srcs[RT_LOGICAL_SRC_SYNCHRONOUS] = brw_imm_ud(synchronous);
      bld.emit(RT_OPCODE_TRACE_RAY_LOGICAL, bld.null_reg_ud(),
               srcs, RT_LOGICAL_NUM_SRCS);

      /* There is no actual value to use in the destination register of the
       * synchronous trace instruction. All of the communication with the HW
       * unit happens through memory reads/writes. So to ensure that the
       * operation has completed before we go read the results in memory, we
       * need a barrier followed by an invalidate before accessing memory.
       */
      if (synchronous) {
         bld.emit(BRW_OPCODE_SYNC, bld.null_reg_ud(), brw_imm_ud(TGL_SYNC_ALLWR));
         emit_rt_lsc_fence(bld, LSC_FENCE_LOCAL, LSC_FLUSH_TYPE_INVALIDATE);
      }
      break;
   }

   default:
#ifndef NDEBUG
      assert(instr->intrinsic < nir_num_intrinsics);
      fprintf(stderr, "intrinsic: %s\n", nir_intrinsic_infos[instr->intrinsic].name);
#endif
      unreachable("unknown intrinsic");
   }
}

static fs_reg
expand_to_32bit(const fs_builder &bld, const fs_reg &src)
{
   if (type_sz(src.type) == 2) {
      fs_reg src32 = bld.vgrf(BRW_REGISTER_TYPE_UD);
      bld.MOV(src32, retype(src, BRW_REGISTER_TYPE_UW));
      return src32;
   } else {
      return src;
   }
}

void
fs_visitor::nir_emit_surface_atomic(const fs_builder &bld,
                                    nir_intrinsic_instr *instr,
                                    fs_reg surface,
                                    bool bindless)
{
   enum lsc_opcode op = lsc_aop_for_nir_intrinsic(instr);
   int num_data = lsc_op_num_data_values(op);

   bool shared = surface.file == IMM && surface.ud == GFX7_BTI_SLM;

   /* The BTI untyped atomic messages only support 32-bit atomics.  If you
    * just look at the big table of messages in the Vol 7 of the SKL PRM, they
    * appear to exist.  However, if you look at Vol 2a, there are no message
    * descriptors provided for Qword atomic ops except for A64 messages.
    *
    * 16-bit float atomics are supported, however.
    */
   assert(nir_dest_bit_size(instr->dest) == 32 ||
          (nir_dest_bit_size(instr->dest) == 64 && devinfo->has_lsc) ||
          (nir_dest_bit_size(instr->dest) == 16 &&
           (devinfo->has_lsc || lsc_opcode_is_atomic_float(op))));

   fs_reg dest = get_nir_dest(instr->dest);

   fs_reg srcs[SURFACE_LOGICAL_NUM_SRCS];
   srcs[bindless ?
        SURFACE_LOGICAL_SRC_SURFACE_HANDLE :
        SURFACE_LOGICAL_SRC_SURFACE] = surface;
   srcs[SURFACE_LOGICAL_SRC_IMM_DIMS] = brw_imm_ud(1);
   srcs[SURFACE_LOGICAL_SRC_IMM_ARG] = brw_imm_ud(op);
   srcs[SURFACE_LOGICAL_SRC_ALLOW_SAMPLE_MASK] = brw_imm_ud(1);

   if (shared) {
      /* SLM - Get the offset */
      if (nir_src_is_const(instr->src[0])) {
         srcs[SURFACE_LOGICAL_SRC_ADDRESS] =
            brw_imm_ud(nir_intrinsic_base(instr) +
                       nir_src_as_uint(instr->src[0]));
      } else {
         srcs[SURFACE_LOGICAL_SRC_ADDRESS] = vgrf(glsl_type::uint_type);
         bld.ADD(srcs[SURFACE_LOGICAL_SRC_ADDRESS],
                 retype(get_nir_src(instr->src[0]), BRW_REGISTER_TYPE_UD),
                 brw_imm_ud(nir_intrinsic_base(instr)));
      }
   } else {
      /* SSBOs */
      srcs[SURFACE_LOGICAL_SRC_ADDRESS] = get_nir_src(instr->src[1]);
   }

   fs_reg data;
   if (num_data >= 1)
      data = expand_to_32bit(bld, get_nir_src(instr->src[shared ? 1 : 2]));

   if (num_data >= 2) {
      fs_reg tmp = bld.vgrf(data.type, 2);
      fs_reg sources[2] = {
         data,
         expand_to_32bit(bld, get_nir_src(instr->src[shared ? 2 : 3]))
      };
      bld.LOAD_PAYLOAD(tmp, sources, 2, 0);
      data = tmp;
   }
   srcs[SURFACE_LOGICAL_SRC_DATA] = data;

   /* Emit the actual atomic operation */

   switch (nir_dest_bit_size(instr->dest)) {
      case 16: {
         fs_reg dest32 = bld.vgrf(BRW_REGISTER_TYPE_UD);
         bld.emit(SHADER_OPCODE_UNTYPED_ATOMIC_LOGICAL,
                  retype(dest32, dest.type),
                  srcs, SURFACE_LOGICAL_NUM_SRCS);
         bld.MOV(retype(dest, BRW_REGISTER_TYPE_UW),
                 retype(dest32, BRW_REGISTER_TYPE_UD));
         break;
      }

      case 32:
      case 64:
         bld.emit(SHADER_OPCODE_UNTYPED_ATOMIC_LOGICAL,
                  dest, srcs, SURFACE_LOGICAL_NUM_SRCS);
         break;
      default:
         unreachable("Unsupported bit size");
   }
}

void
fs_visitor::nir_emit_global_atomic(const fs_builder &bld,
                                   nir_intrinsic_instr *instr)
{
   int op = lsc_aop_for_nir_intrinsic(instr);

   fs_reg dest = get_nir_dest(instr->dest);

   fs_reg addr = get_nir_src(instr->src[0]);

   fs_reg data;
   if (op != LSC_OP_ATOMIC_INC && op != LSC_OP_ATOMIC_DEC)
      data = expand_to_32bit(bld, get_nir_src(instr->src[1]));

   if (op == LSC_OP_ATOMIC_CMPXCHG) {
      fs_reg tmp = bld.vgrf(data.type, 2);
      fs_reg sources[2] = {
         data,
         expand_to_32bit(bld, get_nir_src(instr->src[2]))
      };
      bld.LOAD_PAYLOAD(tmp, sources, 2, 0);
      data = tmp;
   }

   fs_reg srcs[A64_LOGICAL_NUM_SRCS];
   srcs[A64_LOGICAL_ADDRESS] = addr;
   srcs[A64_LOGICAL_SRC] = data;
   srcs[A64_LOGICAL_ARG] = brw_imm_ud(op);
   srcs[A64_LOGICAL_ENABLE_HELPERS] = brw_imm_ud(0);

   switch (nir_dest_bit_size(instr->dest)) {
   case 16: {
      fs_reg dest32 = bld.vgrf(BRW_REGISTER_TYPE_UD);
      bld.emit(SHADER_OPCODE_A64_UNTYPED_ATOMIC_LOGICAL,
               retype(dest32, dest.type),
               srcs, A64_LOGICAL_NUM_SRCS);
      bld.MOV(retype(dest, BRW_REGISTER_TYPE_UW), dest32);
      break;
   }
   case 32:
   case 64:
      bld.emit(SHADER_OPCODE_A64_UNTYPED_ATOMIC_LOGICAL, dest,
               srcs, A64_LOGICAL_NUM_SRCS);
      break;
   default:
      unreachable("Unsupported bit size");
   }
}

void
fs_visitor::nir_emit_texture(const fs_builder &bld, nir_tex_instr *instr)
{
   fs_reg srcs[TEX_LOGICAL_NUM_SRCS];

   int lod_components = 0;

   /* The hardware requires a LOD for buffer textures */
   if (instr->sampler_dim == GLSL_SAMPLER_DIM_BUF)
      srcs[TEX_LOGICAL_SRC_LOD] = brw_imm_d(0);

   uint32_t header_bits = 0;
   for (unsigned i = 0; i < instr->num_srcs; i++) {
      nir_src nir_src = instr->src[i].src;
      fs_reg src = get_nir_src(nir_src);
      switch (instr->src[i].src_type) {
      case nir_tex_src_bias:
         srcs[TEX_LOGICAL_SRC_LOD] =
            retype(get_nir_src_imm(instr->src[i].src), BRW_REGISTER_TYPE_F);
         break;
      case nir_tex_src_comparator:
         srcs[TEX_LOGICAL_SRC_SHADOW_C] = retype(src, BRW_REGISTER_TYPE_F);
         break;
      case nir_tex_src_coord:
         switch (instr->op) {
         case nir_texop_txf:
         case nir_texop_txf_ms:
         case nir_texop_txf_ms_mcs_intel:
         case nir_texop_samples_identical:
            srcs[TEX_LOGICAL_SRC_COORDINATE] = retype(src, BRW_REGISTER_TYPE_D);
            break;
         default:
            srcs[TEX_LOGICAL_SRC_COORDINATE] = retype(src, BRW_REGISTER_TYPE_F);
            break;
         }

         /* Wa_14012688258:
          *
          * Compiler should send U,V,R parameters even if V,R are 0.
          */
         if (instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE &&
             intel_needs_workaround(devinfo, 14012688258))
            assert(instr->coord_components >= 3u);
         break;
      case nir_tex_src_ddx:
         srcs[TEX_LOGICAL_SRC_LOD] = retype(src, BRW_REGISTER_TYPE_F);
         lod_components = nir_tex_instr_src_size(instr, i);
         break;
      case nir_tex_src_ddy:
         srcs[TEX_LOGICAL_SRC_LOD2] = retype(src, BRW_REGISTER_TYPE_F);
         break;
      case nir_tex_src_lod:
         switch (instr->op) {
         case nir_texop_txs:
            srcs[TEX_LOGICAL_SRC_LOD] =
               retype(get_nir_src_imm(instr->src[i].src), BRW_REGISTER_TYPE_UD);
            break;
         case nir_texop_txf:
            srcs[TEX_LOGICAL_SRC_LOD] =
               retype(get_nir_src_imm(instr->src[i].src), BRW_REGISTER_TYPE_D);
            break;
         default:
            srcs[TEX_LOGICAL_SRC_LOD] =
               retype(get_nir_src_imm(instr->src[i].src), BRW_REGISTER_TYPE_F);
            break;
         }
         break;
      case nir_tex_src_min_lod:
         srcs[TEX_LOGICAL_SRC_MIN_LOD] =
            retype(get_nir_src_imm(instr->src[i].src), BRW_REGISTER_TYPE_F);
         break;
      case nir_tex_src_ms_index:
         srcs[TEX_LOGICAL_SRC_SAMPLE_INDEX] = retype(src, BRW_REGISTER_TYPE_UD);
         break;

      case nir_tex_src_offset: {
         uint32_t offset_bits = 0;
         if (brw_texture_offset(instr, i, &offset_bits)) {
            header_bits |= offset_bits;
         } else {
            /* On gfx12.5+, if the offsets are not both constant and in the
             * {-8,7} range, nir_lower_tex() will have already lowered the
             * source offset. So we should never reach this point.
             */
            assert(devinfo->verx10 < 125);
            srcs[TEX_LOGICAL_SRC_TG4_OFFSET] =
               retype(src, BRW_REGISTER_TYPE_D);
         }
         break;
      }

      case nir_tex_src_projector:
         unreachable("should be lowered");

      case nir_tex_src_texture_offset: {
         assert(srcs[TEX_LOGICAL_SRC_SURFACE].file == BAD_FILE);
         /* Emit code to evaluate the actual indexing expression */
         if (instr->texture_index == 0 && is_resource_src(nir_src))
            srcs[TEX_LOGICAL_SRC_SURFACE] = get_resource_nir_src(nir_src);
         if (srcs[TEX_LOGICAL_SRC_SURFACE].file == BAD_FILE) {
            fs_reg tmp = vgrf(glsl_type::uint_type);
            bld.ADD(tmp, src, brw_imm_ud(instr->texture_index));
            srcs[TEX_LOGICAL_SRC_SURFACE] = bld.emit_uniformize(tmp);
         }
         assert(srcs[TEX_LOGICAL_SRC_SURFACE].file != BAD_FILE);
         break;
      }

      case nir_tex_src_sampler_offset: {
         /* Emit code to evaluate the actual indexing expression */
         if (instr->sampler_index == 0 && is_resource_src(nir_src))
            srcs[TEX_LOGICAL_SRC_SAMPLER] = get_resource_nir_src(nir_src);
         if (srcs[TEX_LOGICAL_SRC_SAMPLER].file == BAD_FILE) {
            fs_reg tmp = vgrf(glsl_type::uint_type);
            bld.ADD(tmp, src, brw_imm_ud(instr->sampler_index));
            srcs[TEX_LOGICAL_SRC_SAMPLER] = bld.emit_uniformize(tmp);
         }
         break;
      }

      case nir_tex_src_texture_handle:
         assert(nir_tex_instr_src_index(instr, nir_tex_src_texture_offset) == -1);
         srcs[TEX_LOGICAL_SRC_SURFACE] = fs_reg();
         if (is_resource_src(nir_src))
            srcs[TEX_LOGICAL_SRC_SURFACE_HANDLE] = get_resource_nir_src(nir_src);
         if (srcs[TEX_LOGICAL_SRC_SURFACE_HANDLE].file == BAD_FILE)
            srcs[TEX_LOGICAL_SRC_SURFACE_HANDLE] = bld.emit_uniformize(src);
         break;

      case nir_tex_src_sampler_handle:
         assert(nir_tex_instr_src_index(instr, nir_tex_src_sampler_offset) == -1);
         srcs[TEX_LOGICAL_SRC_SAMPLER] = fs_reg();
         if (is_resource_src(nir_src))
            srcs[TEX_LOGICAL_SRC_SAMPLER_HANDLE] = get_resource_nir_src(nir_src);
         if (srcs[TEX_LOGICAL_SRC_SAMPLER_HANDLE].file == BAD_FILE)
            srcs[TEX_LOGICAL_SRC_SAMPLER_HANDLE] = bld.emit_uniformize(src);
         break;

      case nir_tex_src_ms_mcs_intel:
         assert(instr->op == nir_texop_txf_ms);
         srcs[TEX_LOGICAL_SRC_MCS] = retype(src, BRW_REGISTER_TYPE_D);
         break;

      default:
         unreachable("unknown texture source");
      }
   }

   /* If the surface or sampler were not specified through sources, use the
    * instruction index.
    */
   if (srcs[TEX_LOGICAL_SRC_SURFACE].file == BAD_FILE &&
       srcs[TEX_LOGICAL_SRC_SURFACE_HANDLE].file == BAD_FILE)
      srcs[TEX_LOGICAL_SRC_SURFACE] = brw_imm_ud(instr->texture_index);
   if (srcs[TEX_LOGICAL_SRC_SAMPLER].file == BAD_FILE &&
       srcs[TEX_LOGICAL_SRC_SAMPLER_HANDLE].file == BAD_FILE)
      srcs[TEX_LOGICAL_SRC_SAMPLER] = brw_imm_ud(instr->sampler_index);

   if (srcs[TEX_LOGICAL_SRC_MCS].file == BAD_FILE &&
       (instr->op == nir_texop_txf_ms ||
        instr->op == nir_texop_samples_identical)) {
      if (devinfo->ver >= 7) {
         srcs[TEX_LOGICAL_SRC_MCS] =
            emit_mcs_fetch(srcs[TEX_LOGICAL_SRC_COORDINATE],
                           instr->coord_components,
                           srcs[TEX_LOGICAL_SRC_SURFACE],
                           srcs[TEX_LOGICAL_SRC_SURFACE_HANDLE]);
      } else {
         srcs[TEX_LOGICAL_SRC_MCS] = brw_imm_ud(0u);
      }
   }

   srcs[TEX_LOGICAL_SRC_COORD_COMPONENTS] = brw_imm_d(instr->coord_components);
   srcs[TEX_LOGICAL_SRC_GRAD_COMPONENTS] = brw_imm_d(lod_components);

   enum opcode opcode;
   switch (instr->op) {
   case nir_texop_tex:
      opcode = SHADER_OPCODE_TEX_LOGICAL;
      break;
   case nir_texop_txb:
      opcode = FS_OPCODE_TXB_LOGICAL;
      break;
   case nir_texop_txl:
      opcode = SHADER_OPCODE_TXL_LOGICAL;
      break;
   case nir_texop_txd:
      opcode = SHADER_OPCODE_TXD_LOGICAL;
      break;
   case nir_texop_txf:
      opcode = SHADER_OPCODE_TXF_LOGICAL;
      break;
   case nir_texop_txf_ms:
      /* On Gfx12HP there is only CMS_W available. From the Bspec: Shared
       * Functions - 3D Sampler - Messages - Message Format:
       *
       *   ld2dms REMOVEDBY(GEN:HAS:1406788836)
       */
      if (devinfo->verx10 >= 125)
         opcode = SHADER_OPCODE_TXF_CMS_W_GFX12_LOGICAL;
      else if (devinfo->ver >= 9)
         opcode = SHADER_OPCODE_TXF_CMS_W_LOGICAL;
      else
         opcode = SHADER_OPCODE_TXF_CMS_LOGICAL;
      break;
   case nir_texop_txf_ms_mcs_intel:
      opcode = SHADER_OPCODE_TXF_MCS_LOGICAL;
      break;
   case nir_texop_query_levels:
   case nir_texop_txs:
      opcode = SHADER_OPCODE_TXS_LOGICAL;
      break;
   case nir_texop_lod:
      opcode = SHADER_OPCODE_LOD_LOGICAL;
      break;
   case nir_texop_tg4:
      if (srcs[TEX_LOGICAL_SRC_TG4_OFFSET].file != BAD_FILE)
         opcode = SHADER_OPCODE_TG4_OFFSET_LOGICAL;
      else
         opcode = SHADER_OPCODE_TG4_LOGICAL;
      break;
   case nir_texop_texture_samples:
      opcode = SHADER_OPCODE_SAMPLEINFO_LOGICAL;
      break;
   case nir_texop_samples_identical: {
      fs_reg dst = retype(get_nir_dest(instr->dest), BRW_REGISTER_TYPE_D);

      /* If mcs is an immediate value, it means there is no MCS.  In that case
       * just return false.
       */
      if (srcs[TEX_LOGICAL_SRC_MCS].file == BRW_IMMEDIATE_VALUE) {
         bld.MOV(dst, brw_imm_ud(0u));
      } else if (devinfo->ver >= 9) {
         fs_reg tmp = vgrf(glsl_type::uint_type);
         bld.OR(tmp, srcs[TEX_LOGICAL_SRC_MCS],
                offset(srcs[TEX_LOGICAL_SRC_MCS], bld, 1));
         bld.CMP(dst, tmp, brw_imm_ud(0u), BRW_CONDITIONAL_EQ);
      } else {
         bld.CMP(dst, srcs[TEX_LOGICAL_SRC_MCS], brw_imm_ud(0u),
                 BRW_CONDITIONAL_EQ);
      }
      return;
   }
   default:
      unreachable("unknown texture opcode");
   }

   if (instr->op == nir_texop_tg4) {
      if (instr->component == 1 &&
          key_tex->gather_channel_quirk_mask & (1 << instr->texture_index)) {
         /* gather4 sampler is broken for green channel on RG32F --
          * we must ask for blue instead.
          */
         header_bits |= 2 << 16;
      } else {
         header_bits |= instr->component << 16;
      }
   }

   fs_reg dst = bld.vgrf(brw_type_for_nir_type(devinfo, instr->dest_type), 4);
   fs_inst *inst = bld.emit(opcode, dst, srcs, ARRAY_SIZE(srcs));
   inst->offset = header_bits;

   const unsigned dest_size = nir_tex_instr_dest_size(instr);
   if (devinfo->ver >= 9 &&
       instr->op != nir_texop_tg4 && instr->op != nir_texop_query_levels) {
      unsigned write_mask = instr->dest.is_ssa ?
                            nir_ssa_def_components_read(&instr->dest.ssa):
                            (1 << dest_size) - 1;
      assert(write_mask != 0); /* dead code should have been eliminated */
      inst->size_written = util_last_bit(write_mask) *
                           inst->dst.component_size(inst->exec_size);
   } else {
      inst->size_written = 4 * inst->dst.component_size(inst->exec_size);
   }

   if (srcs[TEX_LOGICAL_SRC_SHADOW_C].file != BAD_FILE)
      inst->shadow_compare = true;

   fs_reg nir_dest[5];
   for (unsigned i = 0; i < dest_size; i++)
      nir_dest[i] = offset(dst, bld, i);

   if (instr->op == nir_texop_query_levels) {
      /* # levels is in .w */
      if (devinfo->ver <= 9) {
         /**
          * Wa_1940217:
          *
          * When a surface of type SURFTYPE_NULL is accessed by resinfo, the
          * MIPCount returned is undefined instead of 0.
          */
         fs_inst *mov = bld.MOV(bld.null_reg_d(), dst);
         mov->conditional_mod = BRW_CONDITIONAL_NZ;
         nir_dest[0] = bld.vgrf(BRW_REGISTER_TYPE_D);
         fs_inst *sel = bld.SEL(nir_dest[0], offset(dst, bld, 3), brw_imm_d(0));
         sel->predicate = BRW_PREDICATE_NORMAL;
      } else {
         nir_dest[0] = offset(dst, bld, 3);
      }
   } else if (instr->op == nir_texop_txs &&
              dest_size >= 3 && devinfo->ver < 7) {
      /* Gfx4-6 return 0 instead of 1 for single layer surfaces. */
      fs_reg depth = offset(dst, bld, 2);
      nir_dest[2] = vgrf(glsl_type::int_type);
      bld.emit_minmax(nir_dest[2], depth, brw_imm_d(1), BRW_CONDITIONAL_GE);
   }

   bld.LOAD_PAYLOAD(get_nir_dest(instr->dest), nir_dest, dest_size, 0);
}

void
fs_visitor::nir_emit_jump(const fs_builder &bld, nir_jump_instr *instr)
{
   switch (instr->type) {
   case nir_jump_break:
      bld.emit(BRW_OPCODE_BREAK);
      break;
   case nir_jump_continue:
      bld.emit(BRW_OPCODE_CONTINUE);
      break;
   case nir_jump_halt:
      bld.emit(BRW_OPCODE_HALT);
      break;
   case nir_jump_return:
   default:
      unreachable("unknown jump");
   }
}

/*
 * This helper takes a source register and un/shuffles it into the destination
 * register.
 *
 * If source type size is smaller than destination type size the operation
 * needed is a component shuffle. The opposite case would be an unshuffle. If
 * source/destination type size is equal a shuffle is done that would be
 * equivalent to a simple MOV.
 *
 * For example, if source is a 16-bit type and destination is 32-bit. A 3
 * components .xyz 16-bit vector on SIMD8 would be.
 *
 *    |x1|x2|x3|x4|x5|x6|x7|x8|y1|y2|y3|y4|y5|y6|y7|y8|
 *    |z1|z2|z3|z4|z5|z6|z7|z8|  |  |  |  |  |  |  |  |
 *
 * This helper will return the following 2 32-bit components with the 16-bit
 * values shuffled:
 *
 *    |x1 y1|x2 y2|x3 y3|x4 y4|x5 y5|x6 y6|x7 y7|x8 y8|
 *    |z1   |z2   |z3   |z4   |z5   |z6   |z7   |z8   |
 *
 * For unshuffle, the example would be the opposite, a 64-bit type source
 * and a 32-bit destination. A 2 component .xy 64-bit vector on SIMD8
 * would be:
 *
 *    | x1l   x1h | x2l   x2h | x3l   x3h | x4l   x4h |
 *    | x5l   x5h | x6l   x6h | x7l   x7h | x8l   x8h |
 *    | y1l   y1h | y2l   y2h | y3l   y3h | y4l   y4h |
 *    | y5l   y5h | y6l   y6h | y7l   y7h | y8l   y8h |
 *
 * The returned result would be the following 4 32-bit components unshuffled:
 *
 *    | x1l | x2l | x3l | x4l | x5l | x6l | x7l | x8l |
 *    | x1h | x2h | x3h | x4h | x5h | x6h | x7h | x8h |
 *    | y1l | y2l | y3l | y4l | y5l | y6l | y7l | y8l |
 *    | y1h | y2h | y3h | y4h | y5h | y6h | y7h | y8h |
 *
 * - Source and destination register must not be overlapped.
 * - components units are measured in terms of the smaller type between
 *   source and destination because we are un/shuffling the smaller
 *   components from/into the bigger ones.
 * - first_component parameter allows skipping source components.
 */
void
shuffle_src_to_dst(const fs_builder &bld,
                   const fs_reg &dst,
                   const fs_reg &src,
                   uint32_t first_component,
                   uint32_t components)
{
   if (type_sz(src.type) == type_sz(dst.type)) {
      assert(!regions_overlap(dst,
         type_sz(dst.type) * bld.dispatch_width() * components,
         offset(src, bld, first_component),
         type_sz(src.type) * bld.dispatch_width() * components));
      for (unsigned i = 0; i < components; i++) {
         bld.MOV(retype(offset(dst, bld, i), src.type),
                 offset(src, bld, i + first_component));
      }
   } else if (type_sz(src.type) < type_sz(dst.type)) {
      /* Source is shuffled into destination */
      unsigned size_ratio = type_sz(dst.type) / type_sz(src.type);
      assert(!regions_overlap(dst,
         type_sz(dst.type) * bld.dispatch_width() *
         DIV_ROUND_UP(components, size_ratio),
         offset(src, bld, first_component),
         type_sz(src.type) * bld.dispatch_width() * components));

      brw_reg_type shuffle_type =
         brw_reg_type_from_bit_size(8 * type_sz(src.type),
                                    BRW_REGISTER_TYPE_D);
      for (unsigned i = 0; i < components; i++) {
         fs_reg shuffle_component_i =
            subscript(offset(dst, bld, i / size_ratio),
                      shuffle_type, i % size_ratio);
         bld.MOV(shuffle_component_i,
                 retype(offset(src, bld, i + first_component), shuffle_type));
      }
   } else {
      /* Source is unshuffled into destination */
      unsigned size_ratio = type_sz(src.type) / type_sz(dst.type);
      assert(!regions_overlap(dst,
         type_sz(dst.type) * bld.dispatch_width() * components,
         offset(src, bld, first_component / size_ratio),
         type_sz(src.type) * bld.dispatch_width() *
         DIV_ROUND_UP(components + (first_component % size_ratio),
                      size_ratio)));

      brw_reg_type shuffle_type =
         brw_reg_type_from_bit_size(8 * type_sz(dst.type),
                                    BRW_REGISTER_TYPE_D);
      for (unsigned i = 0; i < components; i++) {
         fs_reg shuffle_component_i =
            subscript(offset(src, bld, (first_component + i) / size_ratio),
                      shuffle_type, (first_component + i) % size_ratio);
         bld.MOV(retype(offset(dst, bld, i), shuffle_type),
                 shuffle_component_i);
      }
   }
}

void
shuffle_from_32bit_read(const fs_builder &bld,
                        const fs_reg &dst,
                        const fs_reg &src,
                        uint32_t first_component,
                        uint32_t components)
{
   assert(type_sz(src.type) == 4);

   /* This function takes components in units of the destination type while
    * shuffle_src_to_dst takes components in units of the smallest type
    */
   if (type_sz(dst.type) > 4) {
      assert(type_sz(dst.type) == 8);
      first_component *= 2;
      components *= 2;
   }

   shuffle_src_to_dst(bld, dst, src, first_component, components);
}

fs_reg
setup_imm_df(const fs_builder &bld, double v)
{
   const struct intel_device_info *devinfo = bld.shader->devinfo;
   assert(devinfo->ver >= 7);

   if (devinfo->ver >= 8)
      return brw_imm_df(v);

   /* gfx7.5 does not support DF immediates straightforward but the DIM
    * instruction allows to set the 64-bit immediate value.
    */
   if (devinfo->platform == INTEL_PLATFORM_HSW) {
      const fs_builder ubld = bld.exec_all().group(1, 0);
      fs_reg dst = ubld.vgrf(BRW_REGISTER_TYPE_DF, 1);
      ubld.DIM(dst, brw_imm_df(v));
      return component(dst, 0);
   }

   /* gfx7 does not support DF immediates, so we generate a 64-bit constant by
    * writing the low 32-bit of the constant to suboffset 0 of a VGRF and
    * the high 32-bit to suboffset 4 and then applying a stride of 0.
    *
    * Alternatively, we could also produce a normal VGRF (without stride 0)
    * by writing to all the channels in the VGRF, however, that would hit the
    * gfx7 bug where we have to split writes that span more than 1 register
    * into instructions with a width of 4 (otherwise the write to the second
    * register written runs into an execmask hardware bug) which isn't very
    * nice.
    */
   union {
      double d;
      struct {
         uint32_t i1;
         uint32_t i2;
      };
   } di;

   di.d = v;

   const fs_builder ubld = bld.exec_all().group(1, 0);
   const fs_reg tmp = ubld.vgrf(BRW_REGISTER_TYPE_UD, 2);
   ubld.MOV(tmp, brw_imm_ud(di.i1));
   ubld.MOV(horiz_offset(tmp, 1), brw_imm_ud(di.i2));

   return component(retype(tmp, BRW_REGISTER_TYPE_DF), 0);
}

fs_reg
setup_imm_b(const fs_builder &bld, int8_t v)
{
   const fs_reg tmp = bld.vgrf(BRW_REGISTER_TYPE_B);
   bld.MOV(tmp, brw_imm_w(v));
   return tmp;
}

fs_reg
setup_imm_ub(const fs_builder &bld, uint8_t v)
{
   const fs_reg tmp = bld.vgrf(BRW_REGISTER_TYPE_UB);
   bld.MOV(tmp, brw_imm_uw(v));
   return tmp;
}
