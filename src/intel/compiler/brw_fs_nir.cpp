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

#include "brw_fs.h"
#include "brw_fs_builder.h"
#include "brw_nir.h"
#include "brw_eu.h"
#include "nir.h"
#include "nir_intrinsics.h"
#include "nir_search_helpers.h"
#include "dev/intel_debug.h"
#include "util/u_math.h"
#include "util/bitscan.h"

#include <vector>

using namespace brw;

struct brw_fs_bind_info {
   bool valid;
   bool bindless;
   unsigned block;
   unsigned set;
   unsigned binding;
};

struct nir_to_brw_state {
   fs_visitor &s;
   const nir_shader *nir;
   const intel_device_info *devinfo;
   void *mem_ctx;

   /* Points to the end of the program.  Annotated with the current NIR
    * instruction when applicable.
    */
   fs_builder bld;

   brw_reg *ssa_values;
   fs_inst **resource_insts;
   struct brw_fs_bind_info *ssa_bind_infos;
   brw_reg *uniform_values;
   brw_reg *system_values;

   bool annotate;
};

static brw_reg get_nir_src(nir_to_brw_state &ntb, const nir_src &src);
static brw_reg get_nir_def(nir_to_brw_state &ntb, const nir_def &def);
static nir_component_mask_t get_nir_write_mask(const nir_def &def);

static void fs_nir_emit_intrinsic(nir_to_brw_state &ntb, const fs_builder &bld, nir_intrinsic_instr *instr);
static brw_reg emit_samplepos_setup(nir_to_brw_state &ntb);
static brw_reg emit_sampleid_setup(nir_to_brw_state &ntb);
static brw_reg emit_samplemaskin_setup(nir_to_brw_state &ntb);
static brw_reg emit_shading_rate_setup(nir_to_brw_state &ntb);

static void fs_nir_emit_impl(nir_to_brw_state &ntb, nir_function_impl *impl);
static void fs_nir_emit_cf_list(nir_to_brw_state &ntb, exec_list *list);
static void fs_nir_emit_if(nir_to_brw_state &ntb, nir_if *if_stmt);
static void fs_nir_emit_loop(nir_to_brw_state &ntb, nir_loop *loop);
static void fs_nir_emit_block(nir_to_brw_state &ntb, nir_block *block);
static void fs_nir_emit_instr(nir_to_brw_state &ntb, nir_instr *instr);

static void fs_nir_emit_memory_access(nir_to_brw_state &ntb,
                                      const fs_builder &bld,
                                      nir_intrinsic_instr *instr);

static bool
brw_texture_offset(const nir_tex_instr *tex, unsigned src,
                   uint32_t *offset_bits_out)
{
   if (!nir_src_is_const(tex->src[src].src))
      return false;

   const unsigned num_components = nir_tex_instr_src_size(tex, src);

   /* Combine all three offsets into a single unsigned dword:
    *
    *    bits 11:8 - U Offset (X component)
    *    bits  7:4 - V Offset (Y component)
    *    bits  3:0 - R Offset (Z component)
    */
   uint32_t offset_bits = 0;
   for (unsigned i = 0; i < num_components; i++) {
      int offset = nir_src_comp_as_int(tex->src[src].src, i);

      /* offset out of bounds; caller will handle it. */
      if (offset > 7 || offset < -8)
         return false;

      const unsigned shift = 4 * (2 - i);
      offset_bits |= (offset & 0xF) << shift;
   }

   *offset_bits_out = offset_bits;

   return true;
}

static brw_reg
setup_imm_b(const fs_builder &bld, int8_t v)
{
   const brw_reg tmp = bld.vgrf(BRW_TYPE_B);
   bld.MOV(tmp, brw_imm_w(v));
   return tmp;
}

static void
fs_nir_setup_outputs(nir_to_brw_state &ntb)
{
   fs_visitor &s = ntb.s;

   if (s.stage == MESA_SHADER_TESS_CTRL ||
       s.stage == MESA_SHADER_TASK ||
       s.stage == MESA_SHADER_MESH ||
       s.stage == MESA_SHADER_FRAGMENT ||
       s.stage == MESA_SHADER_COMPUTE)
      return;

   unsigned vec4s[VARYING_SLOT_TESS_MAX] = { 0, };

   /* Calculate the size of output registers in a separate pass, before
    * allocating them.  With ARB_enhanced_layouts, multiple output variables
    * may occupy the same slot, but have different type sizes.
    */
   nir_foreach_shader_out_variable(var, s.nir) {
      const int loc = var->data.driver_location;
      const unsigned var_vec4s = nir_variable_count_slots(var, var->type);
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

      brw_reg reg = ntb.bld.vgrf(BRW_TYPE_F, 4 * reg_size);
      for (unsigned i = 0; i < reg_size; i++) {
         assert(loc + i < ARRAY_SIZE(s.outputs));
         s.outputs[loc + i] = offset(reg, ntb.bld, 4 * i);
      }

      loc += reg_size;
   }
}

static void
fs_nir_setup_uniforms(fs_visitor &s)
{
   const intel_device_info *devinfo = s.devinfo;

   /* Only the first compile gets to set up uniforms. */
   if (s.push_constant_loc)
      return;

   s.uniforms = s.nir->num_uniforms / 4;

   if (gl_shader_stage_is_compute(s.stage) && devinfo->verx10 < 125) {
      /* Add uniforms for builtins after regular NIR uniforms. */
      assert(s.uniforms == s.prog_data->nr_params);

      /* Subgroup ID must be the last uniform on the list.  This will make
       * easier later to split between cross thread and per thread
       * uniforms.
       */
      uint32_t *param = brw_stage_prog_data_add_params(s.prog_data, 1);
      *param = BRW_PARAM_BUILTIN_SUBGROUP_ID;
      s.uniforms++;
   }
}

static brw_reg
emit_work_group_id_setup(nir_to_brw_state &ntb)
{
   fs_visitor &s = ntb.s;
   const fs_builder &bld = ntb.bld;

   assert(gl_shader_stage_is_compute(s.stage));

   brw_reg id = bld.vgrf(BRW_TYPE_UD, 3);

   struct brw_reg r0_1(retype(brw_vec1_grf(0, 1), BRW_TYPE_UD));
   bld.MOV(id, r0_1);

   struct brw_reg r0_6(retype(brw_vec1_grf(0, 6), BRW_TYPE_UD));
   struct brw_reg r0_7(retype(brw_vec1_grf(0, 7), BRW_TYPE_UD));
   bld.MOV(offset(id, bld, 1), r0_6);
   bld.MOV(offset(id, bld, 2), r0_7);

   return id;
}

static bool
emit_system_values_block(nir_to_brw_state &ntb, nir_block *block)
{
   fs_visitor &s = ntb.s;
   brw_reg *reg;

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
         if (!gl_shader_stage_is_mesh(s.stage))
            unreachable("should be lowered by brw_nir_lower_vs_inputs().");
         break;

      case nir_intrinsic_load_invocation_id:
         if (s.stage == MESA_SHADER_TESS_CTRL)
            break;
         assert(s.stage == MESA_SHADER_GEOMETRY);
         reg = &ntb.system_values[SYSTEM_VALUE_INVOCATION_ID];
         if (reg->file == BAD_FILE) {
            *reg = s.gs_payload().instance_id;
         }
         break;

      case nir_intrinsic_load_sample_pos:
      case nir_intrinsic_load_sample_pos_or_center:
         assert(s.stage == MESA_SHADER_FRAGMENT);
         reg = &ntb.system_values[SYSTEM_VALUE_SAMPLE_POS];
         if (reg->file == BAD_FILE)
            *reg = emit_samplepos_setup(ntb);
         break;

      case nir_intrinsic_load_sample_id:
         assert(s.stage == MESA_SHADER_FRAGMENT);
         reg = &ntb.system_values[SYSTEM_VALUE_SAMPLE_ID];
         if (reg->file == BAD_FILE)
            *reg = emit_sampleid_setup(ntb);
         break;

      case nir_intrinsic_load_sample_mask_in:
         assert(s.stage == MESA_SHADER_FRAGMENT);
         reg = &ntb.system_values[SYSTEM_VALUE_SAMPLE_MASK_IN];
         if (reg->file == BAD_FILE)
            *reg = emit_samplemaskin_setup(ntb);
         break;

      case nir_intrinsic_load_workgroup_id:
         if (gl_shader_stage_is_mesh(s.stage))
            unreachable("should be lowered by nir_lower_compute_system_values().");
         assert(gl_shader_stage_is_compute(s.stage));
         reg = &ntb.system_values[SYSTEM_VALUE_WORKGROUP_ID];
         if (reg->file == BAD_FILE)
            *reg = emit_work_group_id_setup(ntb);
         break;

      case nir_intrinsic_load_helper_invocation:
         assert(s.stage == MESA_SHADER_FRAGMENT);
         reg = &ntb.system_values[SYSTEM_VALUE_HELPER_INVOCATION];
         if (reg->file == BAD_FILE) {
            const fs_builder abld =
               ntb.bld.annotate("gl_HelperInvocation");

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
            brw_reg shifted = abld.vgrf(BRW_TYPE_UW);

            for (unsigned i = 0; i < DIV_ROUND_UP(s.dispatch_width, 16); i++) {
               const fs_builder hbld = abld.group(MIN2(16, s.dispatch_width), i);
               /* According to the "PS Thread Payload for Normal
                * Dispatch" pages on the BSpec, the dispatch mask is
                * stored in R0.15/R1.15 on gfx20+ and in R1.7/R2.7 on
                * gfx6+.
                */
               const struct brw_reg reg = s.devinfo->ver >= 20 ?
                  xe2_vec1_grf(i, 15) : brw_vec1_grf(i + 1, 7);
               hbld.SHR(offset(shifted, hbld, i),
                        stride(retype(reg, BRW_TYPE_UB), 1, 8, 0),
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
            brw_reg inverted = negate(shifted);

            /* We then resolve the 0/1 result to 0/~0 boolean values by ANDing
             * with 1 and negating.
             */
            brw_reg anded = abld.vgrf(BRW_TYPE_UD);
            abld.AND(anded, inverted, brw_imm_uw(1));

            *reg = abld.MOV(negate(retype(anded, BRW_TYPE_D)));
         }
         break;

      case nir_intrinsic_load_frag_shading_rate:
         reg = &ntb.system_values[SYSTEM_VALUE_FRAG_SHADING_RATE];
         if (reg->file == BAD_FILE)
            *reg = emit_shading_rate_setup(ntb);
         break;

      default:
         break;
      }
   }

   return true;
}

static void
fs_nir_emit_system_values(nir_to_brw_state &ntb)
{
   fs_visitor &s = ntb.s;

   ntb.system_values = ralloc_array(ntb.mem_ctx, brw_reg, SYSTEM_VALUE_MAX);
   for (unsigned i = 0; i < SYSTEM_VALUE_MAX; i++) {
      ntb.system_values[i] = brw_reg();
   }

   nir_function_impl *impl = nir_shader_get_entrypoint((nir_shader *)s.nir);
   nir_foreach_block(block, impl)
      emit_system_values_block(ntb, block);
}

static void
fs_nir_emit_impl(nir_to_brw_state &ntb, nir_function_impl *impl)
{
   ntb.ssa_values = rzalloc_array(ntb.mem_ctx, brw_reg, impl->ssa_alloc);
   ntb.resource_insts = rzalloc_array(ntb.mem_ctx, fs_inst *, impl->ssa_alloc);
   ntb.ssa_bind_infos = rzalloc_array(ntb.mem_ctx, struct brw_fs_bind_info, impl->ssa_alloc);
   ntb.uniform_values = rzalloc_array(ntb.mem_ctx, brw_reg, impl->ssa_alloc);

   fs_nir_emit_cf_list(ntb, &impl->body);
}

static void
fs_nir_emit_cf_list(nir_to_brw_state &ntb, exec_list *list)
{
   exec_list_validate(list);
   foreach_list_typed(nir_cf_node, node, node, list) {
      switch (node->type) {
      case nir_cf_node_if:
         fs_nir_emit_if(ntb, nir_cf_node_as_if(node));
         break;

      case nir_cf_node_loop:
         fs_nir_emit_loop(ntb, nir_cf_node_as_loop(node));
         break;

      case nir_cf_node_block:
         fs_nir_emit_block(ntb, nir_cf_node_as_block(node));
         break;

      default:
         unreachable("Invalid CFG node block");
      }
   }
}

static void
fs_nir_emit_if(nir_to_brw_state &ntb, nir_if *if_stmt)
{
   const fs_builder &bld = ntb.bld;

   bool invert;
   brw_reg cond_reg;

   /* If the condition has the form !other_condition, use other_condition as
    * the source, but invert the predicate on the if instruction.
    */
   nir_alu_instr *cond = nir_src_as_alu_instr(if_stmt->condition);
   if (cond != NULL && cond->op == nir_op_inot) {
      invert = true;
      cond_reg = get_nir_src(ntb, cond->src[0].src);
      cond_reg = offset(cond_reg, bld, cond->src[0].swizzle[0]);
   } else {
      invert = false;
      cond_reg = get_nir_src(ntb, if_stmt->condition);
   }

   /* first, put the condition into f0 */
   fs_inst *inst = bld.MOV(bld.null_reg_d(),
                           retype(cond_reg, BRW_TYPE_D));
   inst->conditional_mod = BRW_CONDITIONAL_NZ;

   fs_inst *iff = bld.IF(BRW_PREDICATE_NORMAL);
   iff->predicate_inverse = invert;

   fs_nir_emit_cf_list(ntb, &if_stmt->then_list);

   if (!nir_cf_list_is_empty_block(&if_stmt->else_list)) {
      bld.emit(BRW_OPCODE_ELSE);
      fs_nir_emit_cf_list(ntb, &if_stmt->else_list);
   }

   fs_inst *endif = bld.emit(BRW_OPCODE_ENDIF);

   /* Peephole: replace IF-JUMP-ENDIF with predicated jump */
   if (endif->prev->prev == iff) {
      fs_inst *jump = (fs_inst *) endif->prev;
      if (jump->predicate == BRW_PREDICATE_NONE &&
          (jump->opcode == BRW_OPCODE_BREAK ||
           jump->opcode == BRW_OPCODE_CONTINUE)) {
         jump->predicate = iff->predicate;
         jump->predicate_inverse = iff->predicate_inverse;
         iff->exec_node::remove();
         endif->exec_node::remove();
      }
   }
}

static void
fs_nir_emit_loop(nir_to_brw_state &ntb, nir_loop *loop)
{
   const fs_builder &bld = ntb.bld;

   assert(!nir_loop_has_continue_construct(loop));
   bld.emit(BRW_OPCODE_DO);

   fs_nir_emit_cf_list(ntb, &loop->body);

   fs_inst *peep_while = bld.emit(BRW_OPCODE_WHILE);

   /* Peephole: replace (+f0) break; while with (-f0) while */
   fs_inst *peep_break = (fs_inst *) peep_while->prev;

   if (peep_break->opcode == BRW_OPCODE_BREAK &&
       peep_break->predicate != BRW_PREDICATE_NONE) {
      peep_while->predicate = peep_break->predicate;
      peep_while->predicate_inverse = !peep_break->predicate_inverse;
      peep_break->exec_node::remove();
   }
}

static void
fs_nir_emit_block(nir_to_brw_state &ntb, nir_block *block)
{
   fs_builder bld = ntb.bld;

   nir_foreach_instr(instr, block) {
      fs_nir_emit_instr(ntb, instr);
   }

   ntb.bld = bld;
}

/**
 * Recognizes a parent instruction of nir_op_extract_* and changes the type to
 * match instr.
 */
static bool
optimize_extract_to_float(nir_to_brw_state &ntb, nir_alu_instr *instr,
                          const brw_reg &result)
{
   const intel_device_info *devinfo = ntb.devinfo;
   const fs_builder &bld = ntb.bld;

   /* No fast path for f16 (yet) or f64. */
   assert(instr->op == nir_op_i2f32 || instr->op == nir_op_u2f32);

   if (!instr->src[0].src.ssa->parent_instr)
      return false;

   if (instr->src[0].src.ssa->parent_instr->type != nir_instr_type_alu)
      return false;

   nir_alu_instr *src0 =
      nir_instr_as_alu(instr->src[0].src.ssa->parent_instr);

   unsigned bytes;
   bool is_signed;

   switch (src0->op) {
   case nir_op_extract_u8:
   case nir_op_extract_u16:
      bytes = src0->op == nir_op_extract_u8 ? 1 : 2;

      /* i2f(extract_u8(a, b)) and u2f(extract_u8(a, b)) produce the same
       * result. Ditto for extract_u16.
       */
      is_signed = false;
      break;

   case nir_op_extract_i8:
   case nir_op_extract_i16:
      bytes = src0->op == nir_op_extract_i8 ? 1 : 2;

      /* The fast path can't handle u2f(extract_i8(a, b)) because the implicit
       * sign extension of the extract_i8 is lost. For example,
       * u2f(extract_i8(0x0000ff00, 1)) should produce 4294967295.0, but a
       * fast path could either give 255.0 (by implementing the fast path as
       * u2f(extract_u8(x))) or -1.0 (by implementing the fast path as
       * i2f(extract_i8(x))). At one point in time, we incorrectly implemented
       * the former.
       */
      if (instr->op != nir_op_i2f32)
         return false;

      is_signed = true;
      break;

   default:
      return false;
   }

   unsigned element = nir_src_as_uint(src0->src[1].src);

   /* Element type to extract.*/
   const brw_reg_type type = brw_int_type(bytes, is_signed);

   brw_reg op0 = get_nir_src(ntb, src0->src[0].src);
   op0.type = brw_type_for_nir_type(devinfo,
      (nir_alu_type)(nir_op_infos[src0->op].input_types[0] |
                     nir_src_bit_size(src0->src[0].src)));
   op0 = offset(op0, bld, src0->src[0].swizzle[0]);

   /* Bspec "Register Region Restrictions" for Xe says:
    *
    *    "In case of all float point data types used in destination
    *
    *    1. Register Regioning patterns where register data bit location of
    *       the LSB of the channels are changed between source and destination
    *       are not supported on Src0 and Src1 except for broadcast of a
    *       scalar."
    *
    * This restriction is enfored in brw_fs_lower_regioning.  There is no
    * reason to generate an optimized instruction that brw_fs_lower_regioning
    * will have to break up later.
    */
   if (devinfo->verx10 >= 125 && element != 0 && !is_uniform(op0))
      return false;

   bld.MOV(result, subscript(op0, type, element));
   return true;
}

static bool
optimize_frontfacing_ternary(nir_to_brw_state &ntb,
                             nir_alu_instr *instr,
                             const brw_reg &result)
{
   const intel_device_info *devinfo = ntb.devinfo;
   fs_visitor &s = ntb.s;

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

   brw_reg tmp = ntb.bld.vgrf(BRW_TYPE_D);

   if (devinfo->ver >= 20) {
      /* Gfx20+ has separate back-facing bits for each pair of
       * subspans in order to support multiple polygons, so we need to
       * use a <1;8,0> region in order to select the correct word for
       * each channel.  Unfortunately they're no longer aligned to the
       * sign bit of a 16-bit word, so a left shift is necessary.
       */
      brw_reg ff = ntb.bld.vgrf(BRW_TYPE_UW);

      for (unsigned i = 0; i < DIV_ROUND_UP(s.dispatch_width, 16); i++) {
         const fs_builder hbld = ntb.bld.group(16, i);
         const struct brw_reg gi_uw = retype(xe2_vec1_grf(i, 9),
                                             BRW_TYPE_UW);
         hbld.SHL(offset(ff, hbld, i), stride(gi_uw, 1, 8, 0), brw_imm_ud(4));
      }

      if (value1 == -1.0f)
         ff.negate = true;

      ntb.bld.OR(subscript(tmp, BRW_TYPE_UW, 1), ff,
                  brw_imm_uw(0x3f80));

   } else if (devinfo->ver >= 12 && s.max_polygons == 2) {
      /* According to the BSpec "PS Thread Payload for Normal
       * Dispatch", the front/back facing interpolation bit is stored
       * as bit 15 of either the R1.1 or R1.6 poly info field, for the
       * first and second polygons respectively in multipolygon PS
       * dispatch mode.
       */
      assert(s.dispatch_width == 16);

      for (unsigned i = 0; i < s.max_polygons; i++) {
         const fs_builder hbld = ntb.bld.group(8, i);
         struct brw_reg g1 = retype(brw_vec1_grf(1, 1 + 5 * i),
                                    BRW_TYPE_UW);

         if (value1 == -1.0f)
            g1.negate = true;

         hbld.OR(subscript(offset(tmp, hbld, i), BRW_TYPE_UW, 1),
                 g1, brw_imm_uw(0x3f80));
      }

   } else if (devinfo->ver >= 12) {
      /* Bit 15 of g1.1 is 0 if the polygon is front facing. */
      brw_reg g1 = brw_reg(retype(brw_vec1_grf(1, 1), BRW_TYPE_W));

      /* For (gl_FrontFacing ? 1.0 : -1.0), emit:
       *
       *    or(8)  tmp.1<2>W  g1.1<0,1,0>W  0x00003f80W
       *    and(8) dst<1>D    tmp<8,8,1>D   0xbf800000D
       *
       * and negate g1.1<0,1,0>W for (gl_FrontFacing ? -1.0 : 1.0).
       */
      if (value1 == -1.0f)
         g1.negate = true;

      ntb.bld.OR(subscript(tmp, BRW_TYPE_W, 1),
                  g1, brw_imm_uw(0x3f80));
   } else {
      /* Bit 15 of g0.0 is 0 if the polygon is front facing. */
      brw_reg g0 = brw_reg(retype(brw_vec1_grf(0, 0), BRW_TYPE_W));

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

      ntb.bld.OR(subscript(tmp, BRW_TYPE_W, 1),
                  g0, brw_imm_uw(0x3f80));
   }
   ntb.bld.AND(retype(result, BRW_TYPE_D), tmp, brw_imm_d(0xbf800000));

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

static brw_reg
prepare_alu_destination_and_sources(nir_to_brw_state &ntb,
                                    const fs_builder &bld,
                                    nir_alu_instr *instr,
                                    brw_reg *op,
                                    bool need_dest)
{
   const intel_device_info *devinfo = ntb.devinfo;

   brw_reg result =
      need_dest ? get_nir_def(ntb, instr->def) : bld.null_reg_ud();

   result.type = brw_type_for_nir_type(devinfo,
      (nir_alu_type)(nir_op_infos[instr->op].output_type |
                     instr->def.bit_size));

   for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
      op[i] = get_nir_src(ntb, instr->src[i].src);
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
      nir_component_mask_t write_mask = get_nir_write_mask(instr->def);
      assert(util_bitcount(write_mask) == 1);
      channel = ffs(write_mask) - 1;

      result = offset(result, bld, channel);
   }

   for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
      assert(nir_op_infos[instr->op].input_sizes[i] < 2);
      op[i] = offset(op[i], bld, instr->src[i].swizzle[channel]);
   }

   return result;
}

static brw_reg
resolve_source_modifiers(const fs_builder &bld, const brw_reg &src)
{
   return (src.abs || src.negate) ? bld.MOV(src) : src;
}

static void
resolve_inot_sources(nir_to_brw_state &ntb, const fs_builder &bld, nir_alu_instr *instr,
                     brw_reg *op)
{
   for (unsigned i = 0; i < 2; i++) {
      nir_alu_instr *inot_instr = nir_src_as_alu_instr(instr->src[i].src);

      if (inot_instr != NULL && inot_instr->op == nir_op_inot) {
         /* The source of the inot is now the source of instr. */
         prepare_alu_destination_and_sources(ntb, bld, inot_instr, &op[i], false);

         assert(!op[i].negate);
         op[i].negate = true;
      } else {
         op[i] = resolve_source_modifiers(bld, op[i]);
      }
   }
}

static bool
try_emit_b2fi_of_inot(nir_to_brw_state &ntb, const fs_builder &bld,
                      brw_reg result,
                      nir_alu_instr *instr)
{
   const intel_device_info *devinfo = bld.shader->devinfo;

   if (devinfo->verx10 >= 125)
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
   if (instr->def.bit_size != 32 ||
       nir_src_bit_size(inot_instr->src[0].src) != 32)
      return false;

   /* b2[fi](inot(a)) maps a=0 => 1, a=-1 => 0.  Since a can only be 0 or -1,
    * this is float(1 + a).
    */
   brw_reg op;

   prepare_alu_destination_and_sources(ntb, bld, inot_instr, &op, false);

   /* Ignore the saturate modifier, if there is one.  The result of the
    * arithmetic can only be 0 or 1, so the clamping will do nothing anyway.
    */
   bld.ADD(result, op, brw_imm_d(1));

   return true;
}

static bool
is_const_zero(const nir_src &src)
{
   return nir_src_is_const(src) && nir_src_as_int(src) == 0;
}

static void
fs_nir_emit_alu(nir_to_brw_state &ntb, nir_alu_instr *instr,
                bool need_dest)
{
   const intel_device_info *devinfo = ntb.devinfo;
   const fs_builder &bld = ntb.bld;

   fs_inst *inst;
   unsigned execution_mode =
      bld.shader->nir->info.float_controls_execution_mode;

   brw_reg op[NIR_MAX_VEC_COMPONENTS];
   brw_reg result = prepare_alu_destination_and_sources(ntb, bld, instr, op, need_dest);

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
         assert(brw_type_size_bytes(op[i].type) > 1);
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
      brw_reg temp = result;
      bool need_extra_copy = false;

      nir_intrinsic_instr *store_reg =
         nir_store_reg_for_def(&instr->def);
      if (store_reg != NULL) {
         nir_def *dest_reg = store_reg->src[1].ssa;
         for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
            nir_intrinsic_instr *load_reg =
               nir_load_reg_for_def(instr->src[i].src.ssa);
            if (load_reg == NULL)
               continue;

            if (load_reg->src[0].ssa == dest_reg) {
               need_extra_copy = true;
               temp = bld.vgrf(result.type, 4);
               break;
            }
         }
      }

      nir_component_mask_t write_mask = get_nir_write_mask(instr->def);
      unsigned last_bit = util_last_bit(write_mask);

      assert(last_bit <= NIR_MAX_VEC_COMPONENTS);
      brw_reg comps[NIR_MAX_VEC_COMPONENTS];

      for (unsigned i = 0; i < last_bit; i++) {
         if (instr->op == nir_op_mov)
            comps[i] = offset(op[0], bld, instr->src[0].swizzle[i]);
         else
            comps[i] = offset(op[i], bld, instr->src[i].swizzle[0]);
      }

      if (write_mask == (1u << last_bit) - 1) {
         bld.VEC(temp, comps, last_bit);
      } else {
         for (unsigned i = 0; i < last_bit; i++) {
            if (write_mask & (1 << i))
               bld.MOV(offset(temp, bld, i), comps[i]);
         }
      }

      /* In this case the source and destination registers were the same,
       * so we need to insert an extra set of moves in order to deal with
       * any swizzling.
       */
      if (need_extra_copy) {
         for (unsigned i = 0; i < last_bit; i++) {
            if (!(write_mask & (1 << i)))
               continue;

            bld.MOV(offset(result, bld, i), offset(temp, bld, i));
         }
      }
      return;
   }

   case nir_op_i2f32:
   case nir_op_u2f32:
      if (optimize_extract_to_float(ntb, instr, result))
         return;
      bld.MOV(result, op[0]);
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

      assert(brw_type_size_bytes(op[0].type) < 8); /* brw_nir_lower_conversions */
      bld.MOV(result, op[0]);
      break;
   }

   case nir_op_b2i8:
   case nir_op_b2i16:
   case nir_op_b2i32:
   case nir_op_b2i64:
   case nir_op_b2f16:
   case nir_op_b2f32:
   case nir_op_b2f64:
      if (try_emit_b2fi_of_inot(ntb, bld, result, instr))
         break;
      op[0].type = BRW_TYPE_D;
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
      if (result.type == BRW_TYPE_B ||
          result.type == BRW_TYPE_UB ||
          result.type == BRW_TYPE_HF)
         assert(brw_type_size_bytes(op[0].type) < 8); /* brw_nir_lower_conversions */

      if (op[0].type == BRW_TYPE_B ||
          op[0].type == BRW_TYPE_UB ||
          op[0].type == BRW_TYPE_HF)
         assert(brw_type_size_bytes(result.type) < 8); /* brw_nir_lower_conversions */

      bld.MOV(result, op[0]);
      break;

   case nir_op_i2i8:
   case nir_op_u2u8:
      assert(brw_type_size_bytes(op[0].type) < 8); /* brw_nir_lower_conversions */
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
            prepare_alu_destination_and_sources(ntb, bld, extract_instr, op, false);

            const unsigned byte = nir_src_as_uint(extract_instr->src[1].src);
            const brw_reg_type type =
               brw_int_type(1, extract_instr->op == nir_op_extract_i8);

            op[0] = subscript(op[0], type, byte);
         } else if (extract_instr->op == nir_op_extract_u16 ||
                    extract_instr->op == nir_op_extract_i16) {
            prepare_alu_destination_and_sources(ntb, bld, extract_instr, op, false);

            const unsigned word = nir_src_as_uint(extract_instr->src[1].src);
            const brw_reg_type type =
               brw_int_type(2, extract_instr->op == nir_op_extract_i16);

            op[0] = subscript(op[0], type, word);
         }
      }

      bld.MOV(result, op[0]);
      break;
   }

   case nir_op_fsat:
      inst = bld.MOV(result, op[0]);
      inst->saturate = true;
      break;

   case nir_op_fneg:
   case nir_op_ineg:
      op[0].negate = true;
      bld.MOV(result, op[0]);
      break;

   case nir_op_fabs:
   case nir_op_iabs:
      op[0].negate = false;
      op[0].abs = true;
      bld.MOV(result, op[0]);
      break;

   case nir_op_f2f32:
      if (nir_has_any_rounding_mode_enabled(execution_mode)) {
         brw_rnd_mode rnd =
            brw_rnd_mode_from_execution_mode(execution_mode);
         bld.exec_all().emit(SHADER_OPCODE_RND_MODE, bld.null_reg_ud(),
                             brw_imm_d(rnd));
      }

      if (op[0].type == BRW_TYPE_HF)
         assert(brw_type_size_bytes(result.type) < 8); /* brw_nir_lower_conversions */

      bld.MOV(result, op[0]);
      break;

   case nir_op_fsign:
      unreachable("Should have been lowered by brw_nir_lower_fsign.");

   case nir_op_frcp:
      bld.RCP(result, op[0]);
      break;

   case nir_op_fexp2:
      bld.EXP2(result, op[0]);
      break;

   case nir_op_flog2:
      bld.LOG2(result, op[0]);
      break;

   case nir_op_fsin:
      bld.SIN(result, op[0]);
      break;

   case nir_op_fcos:
      bld.COS(result, op[0]);
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
      bld.ADD(result, op[0], op[1]);
      break;

   case nir_op_iadd3:
      assert(instr->def.bit_size < 64);
      bld.ADD3(result, op[0], op[1], op[2]);
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
      assert(instr->def.bit_size < 64);
      bld.AVG(result, op[0], op[1]);
      break;

   case nir_op_ihadd:
   case nir_op_uhadd: {
      assert(instr->def.bit_size < 64);

      op[0] = resolve_source_modifiers(bld, op[0]);
      op[1] = resolve_source_modifiers(bld, op[1]);

      /* AVG(x, y) - ((x ^ y) & 1) */
      brw_reg one = retype(brw_imm_ud(1), result.type);
      bld.ADD(result, bld.AVG(op[0], op[1]),
              negate(bld.AND(bld.XOR(op[0], op[1]), one)));
      break;
   }

   case nir_op_fmul:
      if (nir_has_any_rounding_mode_enabled(execution_mode)) {
         brw_rnd_mode rnd =
            brw_rnd_mode_from_execution_mode(execution_mode);
         bld.exec_all().emit(SHADER_OPCODE_RND_MODE, bld.null_reg_ud(),
                             brw_imm_d(rnd));
      }

      bld.MUL(result, op[0], op[1]);
      break;

   case nir_op_imul_2x32_64:
   case nir_op_umul_2x32_64:
      bld.MUL(result, op[0], op[1]);
      break;

   case nir_op_imul_32x16:
   case nir_op_umul_32x16: {
      const bool ud = instr->op == nir_op_umul_32x16;
      const enum brw_reg_type word_type = ud ? BRW_TYPE_UW : BRW_TYPE_W;
      const enum brw_reg_type dword_type = ud ? BRW_TYPE_UD : BRW_TYPE_D;

      assert(instr->def.bit_size == 32);

      /* Before copy propagation there are no immediate values. */
      assert(op[0].file != IMM && op[1].file != IMM);

      op[1] = subscript(op[1], word_type, 0);

      bld.MUL(result, retype(op[0], dword_type), op[1]);

      break;
   }

   case nir_op_imul:
      assert(instr->def.bit_size < 64);
      bld.MUL(result, op[0], op[1]);
      break;

   case nir_op_imul_high:
   case nir_op_umul_high:
      assert(instr->def.bit_size < 64);
      if (instr->def.bit_size == 32) {
         bld.emit(SHADER_OPCODE_MULH, result, op[0], op[1]);
      } else {
         brw_reg tmp = bld.vgrf(brw_type_with_size(op[0].type, 32));
         bld.MUL(tmp, op[0], op[1]);
         bld.MOV(result, subscript(tmp, result.type, 1));
      }
      break;

   case nir_op_idiv:
   case nir_op_udiv:
      assert(instr->def.bit_size < 64);
      bld.INT_QUOTIENT(result, op[0], op[1]);
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
      assert(instr->def.bit_size < 64);
      bld.INT_REMAINDER(result, op[0], op[1]);
      break;

   case nir_op_imod: {
      /* Get a regular C-style remainder.  If a % b == 0, set the predicate. */
      bld.INT_REMAINDER(result, op[0], op[1]);

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
      bld.XOR(op[0], op[1], &inst);
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
      brw_reg dest = result;

      const uint32_t bit_size =  nir_src_bit_size(instr->src[0].src);
      if (bit_size != 32) {
         dest = bld.vgrf(op[0].type);
         bld.UNDEF(dest);
      }

      bld.CMP(dest, op[0], op[1], brw_cmod_for_nir_comparison(instr->op));

      if (bit_size > 32) {
         bld.MOV(result, subscript(dest, BRW_TYPE_UD, 0));
      } else if(bit_size < 32) {
         /* When we convert the result to 32-bit we need to be careful and do
          * it as a signed conversion to get sign extension (for 32-bit true)
          */
         const brw_reg_type src_type =
            brw_type_with_size(BRW_TYPE_D, bit_size);

         bld.MOV(retype(result, BRW_TYPE_D), retype(dest, src_type));
      }
      break;
   }

   case nir_op_ilt32:
   case nir_op_ult32:
   case nir_op_ige32:
   case nir_op_uge32:
   case nir_op_ieq32:
   case nir_op_ine32: {
      brw_reg dest = result;

      const uint32_t bit_size = brw_type_size_bits(op[0].type);
      if (bit_size != 32) {
         dest = bld.vgrf(op[0].type);
         bld.UNDEF(dest);
      }

      bld.CMP(dest, op[0], op[1],
              brw_cmod_for_nir_comparison(instr->op));

      if (bit_size > 32) {
         bld.MOV(result, subscript(dest, BRW_TYPE_UD, 0));
      } else if (bit_size < 32) {
         /* When we convert the result to 32-bit we need to be careful and do
          * it as a signed conversion to get sign extension (for 32-bit true)
          */
         const brw_reg_type src_type =
            brw_type_with_size(BRW_TYPE_D, bit_size);

         bld.MOV(retype(result, BRW_TYPE_D), retype(dest, src_type));
      }
      break;
   }

   case nir_op_inot: {
      nir_alu_instr *inot_src_instr = nir_src_as_alu_instr(instr->src[0].src);

      if (inot_src_instr != NULL &&
          (inot_src_instr->op == nir_op_ior ||
           inot_src_instr->op == nir_op_ixor ||
           inot_src_instr->op == nir_op_iand)) {
         /* The sources of the source logical instruction are now the
          * sources of the instruction that will be generated.
          */
         prepare_alu_destination_and_sources(ntb, bld, inot_src_instr, op, false);
         resolve_inot_sources(ntb, bld, inot_src_instr, op);

         /* Smash all of the sources and destination to be signed.  This
          * doesn't matter for the operation of the instruction, but cmod
          * propagation fails on unsigned sources with negation (due to
          * fs_inst::can_do_cmod returning false).
          */
         result.type =
            brw_type_for_nir_type(devinfo,
                                  (nir_alu_type)(nir_type_int |
                                                 instr->def.bit_size));
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
      bld.NOT(result, op[0]);
      break;
   }

   case nir_op_ixor:
      resolve_inot_sources(ntb, bld, instr, op);
      bld.XOR(result, op[0], op[1]);
      break;
   case nir_op_ior:
      resolve_inot_sources(ntb, bld, instr, op);
      bld.OR(result, op[0], op[1]);
      break;
   case nir_op_iand:
      resolve_inot_sources(ntb, bld, instr, op);
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
      bld.SQRT(result, op[0]);
      break;

   case nir_op_frsq:
      bld.RSQ(result, op[0]);
      break;

   case nir_op_ftrunc:
      bld.RNDZ(result, op[0]);
      break;

   case nir_op_fceil:
      bld.MOV(result, negate(bld.RNDD(negate(op[0]))));
      break;
   case nir_op_ffloor:
      bld.RNDD(result, op[0]);
      break;
   case nir_op_ffract:
      bld.FRC(result, op[0]);
      break;
   case nir_op_fround_even:
      bld.RNDE(result, op[0]);
      break;

   case nir_op_fquantize2f16: {
      brw_reg tmp16 = bld.vgrf(BRW_TYPE_D);
      brw_reg tmp32 = bld.vgrf(BRW_TYPE_F);

      /* The destination stride must be at least as big as the source stride. */
      tmp16 = subscript(tmp16, BRW_TYPE_HF, 0);

      /* Check for denormal */
      brw_reg abs_src0 = op[0];
      abs_src0.abs = true;
      bld.CMP(bld.null_reg_f(), abs_src0, brw_imm_f(ldexpf(1.0, -14)),
              BRW_CONDITIONAL_L);
      /* Get the appropriately signed zero */
      brw_reg zero = retype(bld.AND(retype(op[0], BRW_TYPE_UD),
                                   brw_imm_ud(0x80000000)), BRW_TYPE_F);
      /* Do the actual F32 -> F16 -> F32 conversion */
      bld.MOV(tmp16, op[0]);
      bld.MOV(tmp32, tmp16);
      /* Select that or zero based on normal status */
      inst = bld.SEL(result, zero, tmp32);
      inst->predicate = BRW_PREDICATE_NORMAL;
      break;
   }

   case nir_op_imin:
   case nir_op_umin:
   case nir_op_fmin:
      bld.emit_minmax(result, op[0], op[1], BRW_CONDITIONAL_L);
      break;

   case nir_op_imax:
   case nir_op_umax:
   case nir_op_fmax:
      bld.emit_minmax(result, op[0], op[1], BRW_CONDITIONAL_GE);
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

   case nir_op_unpack_half_2x16_split_x:
      bld.MOV(result, subscript(op[0], BRW_TYPE_HF, 0));
      break;

   case nir_op_unpack_half_2x16_split_y:
      bld.MOV(result, subscript(op[0], BRW_TYPE_HF, 1));
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
         bld.MOV(result, subscript(op[0], BRW_TYPE_UD, 0));
      else
         bld.MOV(result, subscript(op[0], BRW_TYPE_UD, 1));
      break;
   }

   case nir_op_unpack_32_2x16_split_x:
   case nir_op_unpack_32_2x16_split_y: {
      if (instr->op == nir_op_unpack_32_2x16_split_x)
         bld.MOV(result, subscript(op[0], BRW_TYPE_UW, 0));
      else
         bld.MOV(result, subscript(op[0], BRW_TYPE_UW, 1));
      break;
   }

   case nir_op_fpow:
      bld.POW(result, op[0], op[1]);
      break;

   case nir_op_bitfield_reverse:
      assert(instr->def.bit_size == 32);
      assert(nir_src_bit_size(instr->src[0].src) == 32);
      bld.BFREV(result, op[0]);
      break;

   case nir_op_bit_count:
      assert(instr->def.bit_size == 32);
      assert(nir_src_bit_size(instr->src[0].src) < 64);
      bld.CBIT(result, op[0]);
      break;

   case nir_op_uclz:
      assert(instr->def.bit_size == 32);
      assert(nir_src_bit_size(instr->src[0].src) == 32);
      bld.LZD(retype(result, BRW_TYPE_UD), op[0]);
      break;

   case nir_op_ifind_msb: {
      assert(instr->def.bit_size == 32);
      assert(nir_src_bit_size(instr->src[0].src) == 32);

      brw_reg tmp = bld.FBH(retype(op[0], BRW_TYPE_D));

      /* FBH counts from the MSB side, while GLSL's findMSB() wants the count
       * from the LSB side. If FBH didn't return an error (0xFFFFFFFF), then
       * subtract the result from 31 to convert the MSB count into an LSB
       * count.
       */
      brw_reg count_from_lsb = bld.ADD(negate(tmp), brw_imm_w(31));

      /* The high word of the FBH result will be 0xffff or 0x0000. After
       * calculating 31 - fbh, we can obtain the correct result for
       * ifind_msb(0) by ORing the (sign extended) upper word of the
       * intermediate result.
       */
      bld.OR(result, count_from_lsb, subscript(tmp, BRW_TYPE_W, 1));
      break;
   }

   case nir_op_find_lsb:
      assert(instr->def.bit_size == 32);
      assert(nir_src_bit_size(instr->src[0].src) == 32);
      bld.FBL(result, op[0]);
      break;

   case nir_op_ubitfield_extract:
   case nir_op_ibitfield_extract:
      unreachable("should have been lowered");
   case nir_op_ubfe:
   case nir_op_ibfe:
      assert(instr->def.bit_size < 64);
      bld.BFE(result, op[2], op[1], op[0]);
      break;
   case nir_op_bfm:
      assert(instr->def.bit_size < 64);
      bld.BFI1(result, op[0], op[1]);
      break;
   case nir_op_bfi:
      assert(instr->def.bit_size < 64);

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

   /* With regards to implicit masking of the shift counts for 8- and 16-bit
    * types, the PRMs are **incorrect**. They falsely state that on Gen9+ only
    * the low bits of src1 matching the size of src0 (e.g., 4-bits for W or UW
    * src0) are used. The Bspec (backed by data from experimentation) state
    * that 0x3f is used for Q and UQ types, and 0x1f is used for **all** other
    * types.
    *
    * The match the behavior expected for the NIR opcodes, explicit masks for
    * 8- and 16-bit types must be added.
    */
   case nir_op_ishl:
      if (instr->def.bit_size < 32) {
         bld.SHL(result,
                 op[0],
                 bld.AND(op[1], brw_imm_ud(instr->def.bit_size - 1)));
      } else {
         bld.SHL(result, op[0], op[1]);
      }

      break;
   case nir_op_ishr:
      if (instr->def.bit_size < 32) {
         bld.ASR(result,
                 op[0],
                 bld.AND(op[1], brw_imm_ud(instr->def.bit_size - 1)));
      } else {
         bld.ASR(result, op[0], op[1]);
      }

      break;
   case nir_op_ushr:
      if (instr->def.bit_size < 32) {
         bld.SHR(result,
                 op[0],
                 bld.AND(op[1], brw_imm_ud(instr->def.bit_size - 1)));
      } else {
         bld.SHR(result, op[0], op[1]);
      }

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
      inst = bld.DP4A(retype(result, BRW_TYPE_D),
                      retype(op[2], BRW_TYPE_D),
                      retype(op[0], BRW_TYPE_D),
                      retype(op[1], BRW_TYPE_D));

      if (instr->op == nir_op_sdot_4x8_iadd_sat)
         inst->saturate = true;
      break;

   case nir_op_udot_4x8_uadd:
   case nir_op_udot_4x8_uadd_sat:
      inst = bld.DP4A(retype(result, BRW_TYPE_UD),
                      retype(op[2], BRW_TYPE_UD),
                      retype(op[0], BRW_TYPE_UD),
                      retype(op[1], BRW_TYPE_UD));

      if (instr->op == nir_op_udot_4x8_uadd_sat)
         inst->saturate = true;
      break;

   case nir_op_sudot_4x8_iadd:
   case nir_op_sudot_4x8_iadd_sat:
      inst = bld.DP4A(retype(result, BRW_TYPE_D),
                      retype(op[2], BRW_TYPE_D),
                      retype(op[0], BRW_TYPE_D),
                      retype(op[1], BRW_TYPE_UD));

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

      bld.MAD(result, op[2], op[1], op[0]);
      break;

   case nir_op_flrp:
      if (nir_has_any_rounding_mode_enabled(execution_mode)) {
         brw_rnd_mode rnd =
            brw_rnd_mode_from_execution_mode(execution_mode);
         bld.exec_all().emit(SHADER_OPCODE_RND_MODE, bld.null_reg_ud(),
                             brw_imm_d(rnd));
      }

      bld.LRP(result, op[0], op[1], op[2]);
      break;

   case nir_op_b32csel:
      if (optimize_frontfacing_ternary(ntb, instr, result))
         return;

      bld.CMP(bld.null_reg_d(), op[0], brw_imm_d(0), BRW_CONDITIONAL_NZ);
      inst = bld.SEL(result, op[1], op[2]);
      inst->predicate = BRW_PREDICATE_NORMAL;
      break;

   case nir_op_fcsel:
      bld.CSEL(result, op[1], op[2], op[0], BRW_CONDITIONAL_NZ);
      break;

   case nir_op_fcsel_gt:
      bld.CSEL(result, op[1], op[2], op[0], BRW_CONDITIONAL_G);
      break;

   case nir_op_fcsel_ge:
      bld.CSEL(result, op[1], op[2], op[0], BRW_CONDITIONAL_GE);
      break;

   case nir_op_extract_u8:
   case nir_op_extract_i8: {
      const brw_reg_type type = brw_int_type(1, instr->op == nir_op_extract_i8);
      unsigned byte = nir_src_as_uint(instr->src[1].src);

      /* The PRMs say:
       *
       *    BDW+
       *    There is no direct conversion from B/UB to Q/UQ or Q/UQ to B/UB.
       *    Use two instructions and a word or DWord intermediate integer type.
       */
      if (instr->def.bit_size == 64) {
         if (instr->op == nir_op_extract_i8) {
            /* If we need to sign extend, extract to a word first */
            brw_reg w_temp = bld.vgrf(BRW_TYPE_W);
            bld.MOV(w_temp, subscript(op[0], type, byte));
            bld.MOV(result, w_temp);
         } else if (byte & 1) {
            /* Extract the high byte from the word containing the desired byte
             * offset.
             */
            bld.SHR(result,
                    subscript(op[0], BRW_TYPE_UW, byte / 2),
                    brw_imm_uw(8));
         } else {
            /* Otherwise use an AND with 0xff and a word type */
            bld.AND(result,
                    subscript(op[0], BRW_TYPE_UW, byte / 2),
                    brw_imm_uw(0xff));
         }
      } else {
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
}

static void
fs_nir_emit_load_const(nir_to_brw_state &ntb,
                       nir_load_const_instr *instr)
{
   const intel_device_info *devinfo = ntb.devinfo;
   const fs_builder &bld = ntb.bld;

   const brw_reg_type reg_type =
      brw_type_with_size(BRW_TYPE_D, instr->def.bit_size);
   brw_reg reg = bld.vgrf(reg_type, instr->def.num_components);

   brw_reg comps[NIR_MAX_VEC_COMPONENTS];

   switch (instr->def.bit_size) {
   case 8:
      for (unsigned i = 0; i < instr->def.num_components; i++)
         comps[i] = setup_imm_b(bld, instr->value[i].i8);
      break;

   case 16:
      for (unsigned i = 0; i < instr->def.num_components; i++)
         comps[i] = brw_imm_w(instr->value[i].i16);
      break;

   case 32:
      for (unsigned i = 0; i < instr->def.num_components; i++)
         comps[i] = brw_imm_d(instr->value[i].i32);
      break;

   case 64:
      if (!devinfo->has_64bit_int) {
         reg.type = BRW_TYPE_DF;
         for (unsigned i = 0; i < instr->def.num_components; i++)
            comps[i] = brw_imm_df(instr->value[i].f64);
      } else {
         for (unsigned i = 0; i < instr->def.num_components; i++)
            comps[i] = brw_imm_q(instr->value[i].i64);
      }
      break;

   default:
      unreachable("Invalid bit size");
   }

   bld.VEC(reg, comps, instr->def.num_components);

   ntb.ssa_values[instr->def.index] = reg;
}

static bool
get_nir_src_bindless(nir_to_brw_state &ntb, const nir_src &src)
{
   return ntb.ssa_bind_infos[src.ssa->index].bindless;
}

static bool
is_resource_src(nir_src src)
{
   return src.ssa->parent_instr->type == nir_instr_type_intrinsic &&
          nir_instr_as_intrinsic(src.ssa->parent_instr)->intrinsic == nir_intrinsic_resource_intel;
}

static brw_reg
get_resource_nir_src(nir_to_brw_state &ntb, const nir_src &src)
{
   if (!is_resource_src(src))
      return brw_reg();
   return ntb.uniform_values[src.ssa->index];
}

static brw_reg
get_nir_src(nir_to_brw_state &ntb, const nir_src &src)
{
   nir_intrinsic_instr *load_reg = nir_load_reg_for_def(src.ssa);

   brw_reg reg;
   if (!load_reg) {
      if (nir_src_is_undef(src)) {
         const brw_reg_type reg_type =
            brw_type_with_size(BRW_TYPE_D, src.ssa->bit_size);
         reg = ntb.bld.vgrf(reg_type, src.ssa->num_components);
      } else {
         reg = ntb.ssa_values[src.ssa->index];
      }
   } else {
      nir_intrinsic_instr *decl_reg = nir_reg_get_decl(load_reg->src[0].ssa);
      /* We don't handle indirects on locals */
      assert(nir_intrinsic_base(load_reg) == 0);
      assert(load_reg->intrinsic != nir_intrinsic_load_reg_indirect);
      reg = ntb.ssa_values[decl_reg->def.index];
   }

   /* To avoid floating-point denorm flushing problems, set the type by
    * default to an integer type - instructions that need floating point
    * semantics will set this to F if they need to
    */
   reg.type = brw_type_with_size(BRW_TYPE_D, nir_src_bit_size(src));

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
static brw_reg
get_nir_src_imm(nir_to_brw_state &ntb, const nir_src &src)
{
   assert(nir_src_bit_size(src) == 32);
   return nir_src_is_const(src) ?
          brw_reg(brw_imm_d(nir_src_as_int(src))) : get_nir_src(ntb, src);
}

static brw_reg
get_nir_def(nir_to_brw_state &ntb, const nir_def &def)
{
   const fs_builder &bld = ntb.bld;

   nir_intrinsic_instr *store_reg = nir_store_reg_for_def(&def);
   if (!store_reg) {
      const brw_reg_type reg_type =
         brw_type_with_size(def.bit_size == 8 ? BRW_TYPE_D : BRW_TYPE_F,
                            def.bit_size);
      ntb.ssa_values[def.index] =
         bld.vgrf(reg_type, def.num_components);

      if (def.bit_size * bld.dispatch_width() < 8 * REG_SIZE)
         bld.UNDEF(ntb.ssa_values[def.index]);

      return ntb.ssa_values[def.index];
   } else {
      nir_intrinsic_instr *decl_reg =
         nir_reg_get_decl(store_reg->src[1].ssa);
      /* We don't handle indirects on locals */
      assert(nir_intrinsic_base(store_reg) == 0);
      assert(store_reg->intrinsic != nir_intrinsic_store_reg_indirect);
      return ntb.ssa_values[decl_reg->def.index];
   }
}

static nir_component_mask_t
get_nir_write_mask(const nir_def &def)
{
   nir_intrinsic_instr *store_reg = nir_store_reg_for_def(&def);
   if (!store_reg) {
      return nir_component_mask(def.num_components);
   } else {
      return nir_intrinsic_write_mask(store_reg);
   }
}

static fs_inst *
emit_pixel_interpolater_send(const fs_builder &bld,
                             enum opcode opcode,
                             const brw_reg &dst,
                             const brw_reg &src,
                             const brw_reg &desc,
                             const brw_reg &flag_reg,
                             glsl_interp_mode interpolation)
{
   struct brw_wm_prog_data *wm_prog_data =
      brw_wm_prog_data(bld.shader->prog_data);

   brw_reg srcs[INTERP_NUM_SRCS];
   srcs[INTERP_SRC_OFFSET]       = src;
   srcs[INTERP_SRC_MSG_DESC]     = desc;
   srcs[INTERP_SRC_DYNAMIC_MODE] = flag_reg;

   fs_inst *inst = bld.emit(opcode, dst, srcs, INTERP_NUM_SRCS);
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
 * Return the specified component \p subreg of a per-polygon PS
 * payload register for the polygon corresponding to each channel
 * specified in the provided \p bld.
 *
 * \p reg specifies the payload register in REG_SIZE units for the
 * first polygon dispatched to the thread.  This function requires
 * that subsequent registers on the payload contain the corresponding
 * register for subsequent polygons, one GRF register per polygon, if
 * multiple polygons are being processed by the same PS thread.
 *
 * This can be used to access the value of a "Source Depth and/or W
 * Attribute Vertex Deltas", "Perspective Bary Planes" or
 * "Non-Perspective Bary Planes" payload field conveniently for
 * multiple polygons as a single brw_reg.
 */
static brw_reg
fetch_polygon_reg(const fs_builder &bld, unsigned reg, unsigned subreg)
{
   const fs_visitor *shader = bld.shader;
   assert(shader->stage == MESA_SHADER_FRAGMENT);

   const struct intel_device_info *devinfo = shader->devinfo;
   const unsigned poly_width = shader->dispatch_width / shader->max_polygons;
   const unsigned poly_idx = bld.group() / poly_width;
   assert(bld.group() % poly_width == 0);

   if (bld.dispatch_width() > poly_width) {
      assert(bld.dispatch_width() <= 2 * poly_width);
      const unsigned reg_size = reg_unit(devinfo) * REG_SIZE;
      const unsigned vstride = reg_size / brw_type_size_bytes(BRW_TYPE_F);
      return stride(brw_vec1_grf(reg + reg_unit(devinfo) * poly_idx, subreg),
                    vstride, poly_width, 0);
   } else {
      return brw_vec1_grf(reg + reg_unit(devinfo) * poly_idx, subreg);
   }
}

/**
 * Interpolate per-polygon barycentrics at a specific offset relative
 * to each channel fragment coordinates, optionally using
 * perspective-correct interpolation if requested.  This is mostly
 * useful as replacement for the PI shared function that existed on
 * platforms prior to Xe2, but is expected to work on earlier
 * platforms since we can get the required polygon setup information
 * from the thread payload as far back as ICL.
 */
static void
emit_pixel_interpolater_alu_at_offset(const fs_builder &bld,
                                      const brw_reg &dst,
                                      const brw_reg &offs,
                                      glsl_interp_mode interpolation)
{
   const fs_visitor *shader = bld.shader;
   assert(shader->stage == MESA_SHADER_FRAGMENT);

   const intel_device_info *devinfo = shader->devinfo;
   assert(devinfo->ver >= 11);

   const fs_thread_payload &payload = shader->fs_payload();
   const struct brw_wm_prog_data *wm_prog_data =
      brw_wm_prog_data(shader->prog_data);

   if (interpolation == INTERP_MODE_NOPERSPECTIVE) {
      assert(wm_prog_data->uses_npc_bary_coefficients &&
             wm_prog_data->uses_nonperspective_interp_modes);
   } else {
      assert(interpolation == INTERP_MODE_SMOOTH);
      assert(wm_prog_data->uses_pc_bary_coefficients &&
             wm_prog_data->uses_depth_w_coefficients);
   }

   /* Account for half-pixel X/Y coordinate offset. */
   const brw_reg off_x = bld.vgrf(BRW_TYPE_F);
   bld.ADD(off_x, offs, brw_imm_f(0.5));

   const brw_reg off_y = bld.vgrf(BRW_TYPE_F);
   bld.ADD(off_y, offset(offs, bld, 1), brw_imm_f(0.5));

   /* Process no more than two polygons at a time to avoid hitting
    * regioning restrictions.
    */
   const unsigned poly_width = shader->dispatch_width / shader->max_polygons;

   for (unsigned i = 0; i < DIV_ROUND_UP(shader->max_polygons, 2); i++) {
      const fs_builder ibld = bld.group(MIN2(bld.dispatch_width(), 2 * poly_width), i);

      /* Fetch needed parameters from the thread payload. */
      const unsigned bary_coef_reg = interpolation == INTERP_MODE_NOPERSPECTIVE ?
         payload.npc_bary_coef_reg : payload.pc_bary_coef_reg;
      const brw_reg start_x = devinfo->ver < 12 ? fetch_polygon_reg(ibld, 1, 1) :
         fetch_polygon_reg(ibld, bary_coef_reg,
                           devinfo->ver >= 20 ? 6 : 2);
      const brw_reg start_y = devinfo->ver < 12 ? fetch_polygon_reg(ibld, 1, 6) :
         fetch_polygon_reg(ibld, bary_coef_reg,
                           devinfo->ver >= 20 ? 7 : 6);

      const brw_reg bary1_c0 = fetch_polygon_reg(ibld, bary_coef_reg,
                                                devinfo->ver >= 20 ? 2 : 3);
      const brw_reg bary1_cx = fetch_polygon_reg(ibld, bary_coef_reg, 1);
      const brw_reg bary1_cy = fetch_polygon_reg(ibld, bary_coef_reg, 0);

      const brw_reg bary2_c0 = fetch_polygon_reg(ibld, bary_coef_reg,
                                                devinfo->ver >= 20 ? 5 : 7);
      const brw_reg bary2_cx = fetch_polygon_reg(ibld, bary_coef_reg,
                                                devinfo->ver >= 20 ? 4 : 5);
      const brw_reg bary2_cy = fetch_polygon_reg(ibld, bary_coef_reg,
                                                devinfo->ver >= 20 ? 3 : 4);

      const brw_reg rhw_c0 = devinfo->ver >= 20 ?
         fetch_polygon_reg(ibld, payload.depth_w_coef_reg + 1, 5) :
         fetch_polygon_reg(ibld, payload.depth_w_coef_reg, 7);
      const brw_reg rhw_cx = devinfo->ver >= 20 ?
         fetch_polygon_reg(ibld, payload.depth_w_coef_reg + 1, 4) :
         fetch_polygon_reg(ibld, payload.depth_w_coef_reg, 5);
      const brw_reg rhw_cy = devinfo->ver >= 20 ?
         fetch_polygon_reg(ibld, payload.depth_w_coef_reg + 1, 3) :
         fetch_polygon_reg(ibld, payload.depth_w_coef_reg, 4);

      /* Compute X/Y coordinate deltas relative to the origin of the polygon. */
      const brw_reg delta_x = ibld.vgrf(BRW_TYPE_F);
      ibld.ADD(delta_x, offset(shader->pixel_x, ibld, i), negate(start_x));
      ibld.ADD(delta_x, delta_x, offset(off_x, ibld, i));

      const brw_reg delta_y = ibld.vgrf(BRW_TYPE_F);
      ibld.ADD(delta_y, offset(shader->pixel_y, ibld, i), negate(start_y));
      ibld.ADD(delta_y, delta_y, offset(off_y, ibld, i));

      /* Evaluate the plane equations obtained above for the
       * barycentrics and RHW coordinate at the offset specified for
       * each channel.  Limit arithmetic to acc_width in order to
       * allow the accumulator to be used for linear interpolation.
       */
      const unsigned acc_width = 16 * reg_unit(devinfo);
      const brw_reg rhw = ibld.vgrf(BRW_TYPE_F);
      const brw_reg bary1 = ibld.vgrf(BRW_TYPE_F);
      const brw_reg bary2 = ibld.vgrf(BRW_TYPE_F);

      for (unsigned j = 0; j < DIV_ROUND_UP(ibld.dispatch_width(), acc_width); j++) {
         const fs_builder jbld = ibld.group(MIN2(ibld.dispatch_width(), acc_width), j);
         const brw_reg acc = suboffset(brw_acc_reg(16), jbld.group() % acc_width);

         if (interpolation != INTERP_MODE_NOPERSPECTIVE) {
            jbld.MAD(acc, horiz_offset(rhw_c0, acc_width * j),
                     horiz_offset(rhw_cx, acc_width * j), offset(delta_x, jbld, j));
            jbld.MAC(offset(rhw, jbld, j),
                     horiz_offset(rhw_cy, acc_width * j), offset(delta_y, jbld, j));
         }

         jbld.MAD(acc, horiz_offset(bary1_c0, acc_width * j),
                  horiz_offset(bary1_cx, acc_width * j), offset(delta_x, jbld, j));
         jbld.MAC(offset(bary1, jbld, j),
                  horiz_offset(bary1_cy, acc_width * j), offset(delta_y, jbld, j));

         jbld.MAD(acc, horiz_offset(bary2_c0, acc_width * j),
                  horiz_offset(bary2_cx, acc_width * j), offset(delta_x, jbld, j));
         jbld.MAC(offset(bary2, jbld, j),
                  horiz_offset(bary2_cy, acc_width * j), offset(delta_y, jbld, j));
      }

      /* Scale the results dividing by the interpolated RHW coordinate
       * if the interpolation is required to be perspective-correct.
       */
      if (interpolation == INTERP_MODE_NOPERSPECTIVE) {
         ibld.MOV(offset(dst, ibld, i), bary1);
         ibld.MOV(offset(offset(dst, bld, 1), ibld, i), bary2);
      } else {
         const brw_reg w = ibld.vgrf(BRW_TYPE_F);
         ibld.emit(SHADER_OPCODE_RCP, w, rhw);
         ibld.MUL(offset(dst, ibld, i), bary1, w);
         ibld.MUL(offset(offset(dst, bld, 1), ibld, i), bary2, w);
      }
   }
}

/**
 * Interpolate per-polygon barycentrics at a specified sample index,
 * optionally using perspective-correct interpolation if requested.
 * This is mostly useful as replacement for the PI shared function
 * that existed on platforms prior to Xe2, but is expected to work on
 * earlier platforms since we can get the required polygon setup
 * information from the thread payload as far back as ICL.
 */
static void
emit_pixel_interpolater_alu_at_sample(const fs_builder &bld,
                                      const brw_reg &dst,
                                      const brw_reg &idx,
                                      glsl_interp_mode interpolation)
{
   const fs_thread_payload &payload = bld.shader->fs_payload();
   const struct brw_wm_prog_data *wm_prog_data =
      brw_wm_prog_data(bld.shader->prog_data);
   const fs_builder ubld = bld.exec_all().group(16, 0);
   const brw_reg sample_offs_xy = ubld.vgrf(BRW_TYPE_UD);
   assert(wm_prog_data->uses_sample_offsets);

   /* Interleave the X/Y coordinates of each sample in order to allow
    * a single indirect look-up, by using a MOV for the 16 X
    * coordinates, then another MOV for the 16 Y coordinates.
    */
   for (unsigned i = 0; i < 2; i++) {
      const brw_reg reg = retype(brw_vec16_grf(payload.sample_offsets_reg, 4 * i),
                                BRW_TYPE_UB);
      ubld.MOV(subscript(sample_offs_xy, BRW_TYPE_UW, i), reg);
   }

   /* Use indirect addressing to fetch the X/Y offsets of the sample
    * index provided for each channel.
    */
   const brw_reg idx_b = bld.vgrf(BRW_TYPE_UD);
   bld.MUL(idx_b, idx, brw_imm_ud(brw_type_size_bytes(BRW_TYPE_UD)));

   const brw_reg off_xy = bld.vgrf(BRW_TYPE_UD);
   bld.emit(SHADER_OPCODE_MOV_INDIRECT, off_xy, component(sample_offs_xy, 0),
            idx_b, brw_imm_ud(16 * brw_type_size_bytes(BRW_TYPE_UD)));

   /* Convert the selected fixed-point offsets to floating-point
    * offsets.
    */
   const brw_reg offs = bld.vgrf(BRW_TYPE_F, 2);

   for (unsigned i = 0; i < 2; i++) {
      const brw_reg tmp = bld.vgrf(BRW_TYPE_F);
      bld.MOV(tmp, subscript(off_xy, BRW_TYPE_UW, i));
      bld.MUL(tmp, tmp, brw_imm_f(0.0625));
      bld.ADD(offset(offs, bld, i), tmp, brw_imm_f(-0.5));
   }

   /* Interpolate at the resulting offsets. */
   emit_pixel_interpolater_alu_at_offset(bld, dst, offs, interpolation);
}

/**
 * Computes 1 << x, given a D/UD register containing some value x.
 */
static brw_reg
intexp2(const fs_builder &bld, const brw_reg &x)
{
   assert(x.type == BRW_TYPE_UD || x.type == BRW_TYPE_D);

   return bld.SHL(bld.MOV(retype(brw_imm_d(1), x.type)), x);
}

static void
emit_gs_end_primitive(nir_to_brw_state &ntb, const nir_src &vertex_count_nir_src)
{
   fs_visitor &s = ntb.s;
   assert(s.stage == MESA_SHADER_GEOMETRY);

   struct brw_gs_prog_data *gs_prog_data = brw_gs_prog_data(s.prog_data);

   if (s.gs_compile->control_data_header_size_bits == 0)
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
   assert(s.gs_compile->control_data_bits_per_vertex == 1);

   brw_reg vertex_count = get_nir_src(ntb, vertex_count_nir_src);
   vertex_count.type = BRW_TYPE_UD;

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

   const fs_builder abld = ntb.bld.annotate("end primitive");

   /* control_data_bits |= 1 << ((vertex_count - 1) % 32) */
   brw_reg prev_count = abld.ADD(vertex_count, brw_imm_ud(0xffffffffu));
   brw_reg mask = intexp2(abld, prev_count);
   /* Note: we're relying on the fact that the GEN SHL instruction only pays
    * attention to the lower 5 bits of its second source argument, so on this
    * architecture, 1 << (vertex_count - 1) is equivalent to 1 <<
    * ((vertex_count - 1) % 32).
    */
   abld.OR(s.control_data_bits, s.control_data_bits, mask);
}

brw_reg
fs_visitor::gs_urb_per_slot_dword_index(const brw_reg &vertex_count)
{
   /* We use a single UD register to accumulate control data bits (32 bits
    * for each of the SIMD8 channels).  So we need to write a DWord (32 bits)
    * at a time.
    *
    * On platforms < Xe2:
    *    Unfortunately,the URB_WRITE_SIMD8 message uses 128-bit (OWord)
    *    offsets.  We have select a 128-bit group via the Global and Per-Slot
    *    Offsets, then use the Channel Mask phase to enable/disable which DWord
    *    within that group to write.  (Remember, different SIMD8 channels may
    *    have emitted different numbers of vertices, so we may need per-slot
    *    offsets.)
    *
    *    Channel masking presents an annoying problem: we may have to replicate
    *    the data up to 4 times:
    *
    *    Msg = Handles, Per-Slot Offsets, Channel Masks, Data, Data, Data,
    *          Data.
    *
    *    To avoid penalizing shaders that emit a small number of vertices, we
    *    can avoid these sometimes: if the size of the control data header is
    *    <= 128 bits, then there is only 1 OWord.  All SIMD8 channels will land
    *    land in the same 128-bit group, so we can skip per-slot offsets.
    *
    *    Similarly, if the control data header is <= 32 bits, there is only one
    *    DWord, so we can skip channel masks.
    */
   const fs_builder bld = fs_builder(this).at_end();
   const fs_builder abld = bld.annotate("urb per slot offset");

   /* Figure out which DWord we're trying to write to using the formula:
    *
    *    dword_index = (vertex_count - 1) * bits_per_vertex / 32
    *
    * Since bits_per_vertex is a power of two, and is known at compile
    * time, this can be optimized to:
    *
    *    dword_index = (vertex_count - 1) >> (6 - log2(bits_per_vertex))
    */
   brw_reg prev_count = abld.ADD(vertex_count, brw_imm_ud(0xffffffffu));
   unsigned log2_bits_per_vertex =
      util_last_bit(gs_compile->control_data_bits_per_vertex);
   return abld.SHR(prev_count, brw_imm_ud(6u - log2_bits_per_vertex));
}

brw_reg
fs_visitor::gs_urb_channel_mask(const brw_reg &dword_index)
{
   brw_reg channel_mask;

   /* Xe2+ can do URB loads with a byte offset, so we don't need to
    * construct a channel mask.
    */
   if (devinfo->ver >= 20)
      return channel_mask;

   /* Channel masking presents an annoying problem: we may have to replicate
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
   if (gs_compile->control_data_header_size_bits <= 32)
      return channel_mask;

   const fs_builder bld = fs_builder(this).at_end();
   const fs_builder ubld = bld.exec_all();

   /* Set the channel masks to 1 << (dword_index % 4), so that we'll
    * write to the appropriate DWORD within the OWORD.
    */
   brw_reg channel = ubld.AND(dword_index, brw_imm_ud(3u));
   /* Then the channel masks need to be in bits 23:16. */
   return ubld.SHL(intexp2(ubld, channel), brw_imm_ud(16u));
}

void
fs_visitor::emit_gs_control_data_bits(const brw_reg &vertex_count)
{
   assert(stage == MESA_SHADER_GEOMETRY);
   assert(gs_compile->control_data_bits_per_vertex != 0);

   const struct brw_gs_prog_data *gs_prog_data = brw_gs_prog_data(prog_data);

   const fs_builder bld = fs_builder(this).at_end();
   const fs_builder abld = bld.annotate("emit control data bits");

   brw_reg dword_index = gs_urb_per_slot_dword_index(vertex_count);
   brw_reg channel_mask = gs_urb_channel_mask(dword_index);
   brw_reg per_slot_offset;

   const unsigned max_control_data_header_size_bits =
      devinfo->ver >= 20 ? 32 : 128;

   if (gs_compile->control_data_header_size_bits > max_control_data_header_size_bits) {
      /* Convert dword_index to bytes on Xe2+ since LSC can do operate on byte
       * offset granularity.
       */
      if (devinfo->ver >= 20) {
         per_slot_offset = abld.SHL(dword_index, brw_imm_ud(2u));
      } else {
         /* Set the per-slot offset to dword_index / 4, so that we'll write to
          * the appropriate OWord within the control data header.
          */
         per_slot_offset = abld.SHR(dword_index, brw_imm_ud(2u));
      }
   }

   /* If there are channel masks, add 3 extra copies of the data. */
   const unsigned length = 1 + 3 * unsigned(channel_mask.file != BAD_FILE);
   assert(length <= 4);
   brw_reg sources[4];

   for (unsigned i = 0; i < length; i++)
      sources[i] = this->control_data_bits;

   brw_reg srcs[URB_LOGICAL_NUM_SRCS];
   srcs[URB_LOGICAL_SRC_HANDLE] = gs_payload().urb_handles;
   srcs[URB_LOGICAL_SRC_PER_SLOT_OFFSETS] = per_slot_offset;
   srcs[URB_LOGICAL_SRC_CHANNEL_MASK] = channel_mask;
   srcs[URB_LOGICAL_SRC_DATA] = bld.vgrf(BRW_TYPE_F, length);
   srcs[URB_LOGICAL_SRC_COMPONENTS] = brw_imm_ud(length);
   abld.LOAD_PAYLOAD(srcs[URB_LOGICAL_SRC_DATA], sources, length, 0);

   fs_inst *inst = abld.emit(SHADER_OPCODE_URB_WRITE_LOGICAL, reg_undef,
                             srcs, ARRAY_SIZE(srcs));

   /* We need to increment Global Offset by 256-bits to make room for
    * Broadwell's extra "Vertex Count" payload at the beginning of the
    * URB entry.  Since this is an OWord message, Global Offset is counted
    * in 128-bit units, so we must set it to 2.
    */
   if (gs_prog_data->static_vertex_count == -1)
      inst->offset = 2;
}

static void
set_gs_stream_control_data_bits(nir_to_brw_state &ntb, const brw_reg &vertex_count,
                                unsigned stream_id)
{
   fs_visitor &s = ntb.s;

   /* control_data_bits |= stream_id << ((2 * (vertex_count - 1)) % 32) */

   /* Note: we are calling this *before* increasing vertex_count, so
    * this->vertex_count == vertex_count - 1 in the formula above.
    */

   /* Stream mode uses 2 bits per vertex */
   assert(s.gs_compile->control_data_bits_per_vertex == 2);

   /* Must be a valid stream */
   assert(stream_id < 4); /* MAX_VERTEX_STREAMS */

   /* Control data bits are initialized to 0 so we don't have to set any
    * bits when sending vertices to stream 0.
    */
   if (stream_id == 0)
      return;

   const fs_builder abld = ntb.bld.annotate("set stream control data bits");

   /* reg::sid = stream_id */
   brw_reg sid = abld.MOV(brw_imm_ud(stream_id));

   /* reg:shift_count = 2 * (vertex_count - 1) */
   brw_reg shift_count = abld.SHL(vertex_count, brw_imm_ud(1u));

   /* Note: we're relying on the fact that the GEN SHL instruction only pays
    * attention to the lower 5 bits of its second source argument, so on this
    * architecture, stream_id << 2 * (vertex_count - 1) is equivalent to
    * stream_id << ((2 * (vertex_count - 1)) % 32).
    */
   brw_reg mask = abld.SHL(sid, shift_count);
   abld.OR(s.control_data_bits, s.control_data_bits, mask);
}

static void
emit_gs_vertex(nir_to_brw_state &ntb, const nir_src &vertex_count_nir_src,
               unsigned stream_id)
{
   fs_visitor &s = ntb.s;

   assert(s.stage == MESA_SHADER_GEOMETRY);

   struct brw_gs_prog_data *gs_prog_data = brw_gs_prog_data(s.prog_data);

   brw_reg vertex_count = get_nir_src(ntb, vertex_count_nir_src);
   vertex_count.type = BRW_TYPE_UD;

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
   if (stream_id > 0 && !s.nir->info.has_transform_feedback_varyings)
      return;

   /* If we're outputting 32 control data bits or less, then we can wait
    * until the shader is over to output them all.  Otherwise we need to
    * output them as we go.  Now is the time to do it, since we're about to
    * output the vertex_count'th vertex, so it's guaranteed that the
    * control data bits associated with the (vertex_count - 1)th vertex are
    * correct.
    */
   if (s.gs_compile->control_data_header_size_bits > 32) {
      const fs_builder abld =
         ntb.bld.annotate("emit vertex: emit control data bits");

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
         abld.AND(ntb.bld.null_reg_d(), vertex_count,
                  brw_imm_ud(32u / s.gs_compile->control_data_bits_per_vertex - 1u));
      inst->conditional_mod = BRW_CONDITIONAL_Z;

      abld.IF(BRW_PREDICATE_NORMAL);
      /* If vertex_count is 0, then no control data bits have been
       * accumulated yet, so we can skip emitting them.
       */
      abld.CMP(ntb.bld.null_reg_d(), vertex_count, brw_imm_ud(0u),
               BRW_CONDITIONAL_NEQ);
      abld.IF(BRW_PREDICATE_NORMAL);
      s.emit_gs_control_data_bits(vertex_count);
      abld.emit(BRW_OPCODE_ENDIF);

      /* Reset control_data_bits to 0 so we can start accumulating a new
       * batch.
       *
       * Note: in the case where vertex_count == 0, this neutralizes the
       * effect of any call to EndPrimitive() that the shader may have
       * made before outputting its first vertex.
       */
      abld.exec_all().MOV(s.control_data_bits, brw_imm_ud(0u));
      abld.emit(BRW_OPCODE_ENDIF);
   }

   s.emit_urb_writes(vertex_count);

   /* In stream mode we have to set control data bits for all vertices
    * unless we have disabled control data bits completely (which we do
    * do for MESA_PRIM_POINTS outputs that don't use streams).
    */
   if (s.gs_compile->control_data_header_size_bits > 0 &&
       gs_prog_data->control_data_format ==
          GFX7_GS_CONTROL_DATA_FORMAT_GSCTL_SID) {
      set_gs_stream_control_data_bits(ntb, vertex_count, stream_id);
   }
}

static void
brw_combine_with_vec(const fs_builder &bld, const brw_reg &dst,
                     const brw_reg &src, unsigned n)
{
   assert(n <= NIR_MAX_VEC_COMPONENTS);
   brw_reg comps[NIR_MAX_VEC_COMPONENTS];
   for (unsigned i = 0; i < n; i++)
      comps[i] = offset(src, bld, i);
   bld.VEC(dst, comps, n);
}

static void
emit_gs_input_load(nir_to_brw_state &ntb, const brw_reg &dst,
                   const nir_src &vertex_src,
                   unsigned base_offset,
                   const nir_src &offset_src,
                   unsigned num_components,
                   unsigned first_component)
{
   const fs_builder &bld = ntb.bld;
   const struct intel_device_info *devinfo = ntb.devinfo;

   fs_visitor &s = ntb.s;

   assert(brw_type_size_bytes(dst.type) == 4);
   struct brw_gs_prog_data *gs_prog_data = brw_gs_prog_data(s.prog_data);
   const unsigned push_reg_count = gs_prog_data->base.urb_read_length * 8;

   /* TODO: figure out push input layout for invocations == 1 */
   if (gs_prog_data->invocations == 1 &&
       nir_src_is_const(offset_src) && nir_src_is_const(vertex_src) &&
       4 * (base_offset + nir_src_as_uint(offset_src)) < push_reg_count) {
      int imm_offset = (base_offset + nir_src_as_uint(offset_src)) * 4 +
                       nir_src_as_uint(vertex_src) * push_reg_count;

      const brw_reg attr = offset(brw_attr_reg(0, dst.type), bld,
                                  first_component + imm_offset);
      brw_combine_with_vec(bld, dst, attr, num_components);
      return;
   }

   /* Resort to the pull model.  Ensure the VUE handles are provided. */
   assert(gs_prog_data->base.include_vue_handles);

   brw_reg start = s.gs_payload().icp_handle_start;
   brw_reg icp_handle = ntb.bld.vgrf(BRW_TYPE_UD);
   const unsigned grf_size_bytes = REG_SIZE * reg_unit(devinfo);

   if (gs_prog_data->invocations == 1) {
      if (nir_src_is_const(vertex_src)) {
         /* The vertex index is constant; just select the proper URB handle. */
         icp_handle =
            byte_offset(start, nir_src_as_uint(vertex_src) * grf_size_bytes);
      } else {
         /* The vertex index is non-constant.  We need to use indirect
          * addressing to fetch the proper URB handle.
          *
          * First, we start with the sequence <7, 6, 5, 4, 3, 2, 1, 0>
          * indicating that channel <n> should read the handle from
          * DWord <n>.  We convert that to bytes by multiplying by 4.
          *
          * Next, we convert the vertex index to bytes by multiplying
          * by 32/64 (shifting by 5/6), and add the two together.  This is
          * the final indirect byte offset.
          */
         brw_reg sequence = bld.LOAD_SUBGROUP_INVOCATION();

         /* channel_offsets = 4 * sequence = <28, 24, 20, 16, 12, 8, 4, 0> */
         brw_reg channel_offsets = bld.SHL(sequence, brw_imm_ud(2u));
         /* Convert vertex_index to bytes (multiply by 32/64) */
         assert(util_is_power_of_two_nonzero(grf_size_bytes)); /* for ffs() */
         brw_reg vertex_offset_bytes =
            bld.SHL(retype(get_nir_src(ntb, vertex_src), BRW_TYPE_UD),
                    brw_imm_ud(ffs(grf_size_bytes) - 1));
         brw_reg icp_offset_bytes =
            bld.ADD(vertex_offset_bytes, channel_offsets);

         /* Use first_icp_handle as the base offset.  There is one register
          * of URB handles per vertex, so inform the register allocator that
          * we might read up to nir->info.gs.vertices_in registers.
          */
         bld.emit(SHADER_OPCODE_MOV_INDIRECT, icp_handle, start,
                  brw_reg(icp_offset_bytes),
                  brw_imm_ud(s.nir->info.gs.vertices_in * grf_size_bytes));
      }
   } else {
      assert(gs_prog_data->invocations > 1);

      if (nir_src_is_const(vertex_src)) {
         unsigned vertex = nir_src_as_uint(vertex_src);
         bld.MOV(icp_handle, component(start, vertex));
      } else {
         /* The vertex index is non-constant.  We need to use indirect
          * addressing to fetch the proper URB handle.
          *
          * Convert vertex_index to bytes (multiply by 4)
          */
         brw_reg icp_offset_bytes =
            bld.SHL(retype(get_nir_src(ntb, vertex_src), BRW_TYPE_UD),
                    brw_imm_ud(2u));

         /* Use first_icp_handle as the base offset.  There is one DWord
          * of URB handles per vertex, so inform the register allocator that
          * we might read up to ceil(nir->info.gs.vertices_in / 8) registers.
          */
         bld.emit(SHADER_OPCODE_MOV_INDIRECT, icp_handle, start,
                  brw_reg(icp_offset_bytes),
                  brw_imm_ud(DIV_ROUND_UP(s.nir->info.gs.vertices_in, 8) *
                             grf_size_bytes));
      }
   }

   fs_inst *inst;
   brw_reg indirect_offset = get_nir_src(ntb, offset_src);

   if (nir_src_is_const(offset_src)) {
      brw_reg srcs[URB_LOGICAL_NUM_SRCS];
      srcs[URB_LOGICAL_SRC_HANDLE] = icp_handle;

      /* Constant indexing - use global offset. */
      if (first_component != 0) {
         unsigned read_components = num_components + first_component;
         brw_reg tmp = bld.vgrf(dst.type, read_components);
         inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, tmp, srcs,
                         ARRAY_SIZE(srcs));
         inst->size_written = read_components *
                              tmp.component_size(inst->exec_size);
         brw_combine_with_vec(bld, dst, offset(tmp, bld, first_component),
                              num_components);
      } else {
         inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, dst, srcs,
                         ARRAY_SIZE(srcs));
         inst->size_written = num_components *
                              dst.component_size(inst->exec_size);
      }
      inst->offset = base_offset + nir_src_as_uint(offset_src);
   } else {
      /* Indirect indexing - use per-slot offsets as well. */
      unsigned read_components = num_components + first_component;
      brw_reg tmp = bld.vgrf(dst.type, read_components);

      /* Convert oword offset to bytes on Xe2+ */
      if (devinfo->ver >= 20)
         indirect_offset = bld.SHL(indirect_offset, brw_imm_ud(4u));

      brw_reg srcs[URB_LOGICAL_NUM_SRCS];
      srcs[URB_LOGICAL_SRC_HANDLE] = icp_handle;
      srcs[URB_LOGICAL_SRC_PER_SLOT_OFFSETS] = indirect_offset;

      if (first_component != 0) {
         inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, tmp,
                         srcs, ARRAY_SIZE(srcs));
         inst->size_written = read_components *
                              tmp.component_size(inst->exec_size);
         brw_combine_with_vec(bld, dst, offset(tmp, bld, first_component),
                              num_components);
      } else {
         inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, dst,
                         srcs, ARRAY_SIZE(srcs));
         inst->size_written = num_components *
                              dst.component_size(inst->exec_size);
      }
      inst->offset = base_offset;
   }
}

static brw_reg
get_indirect_offset(nir_to_brw_state &ntb, nir_intrinsic_instr *instr)
{
   const intel_device_info *devinfo = ntb.devinfo;
   nir_src *offset_src = nir_get_io_offset_src(instr);

   if (nir_src_is_const(*offset_src)) {
      /* The only constant offset we should find is 0.  brw_nir.c's
       * add_const_offset_to_base() will fold other constant offsets
       * into the "base" index.
       */
      assert(nir_src_as_uint(*offset_src) == 0);
      return brw_reg();
   }

   brw_reg offset = get_nir_src(ntb, *offset_src);

   if (devinfo->ver < 20)
      return offset;

   /* Convert Owords (16-bytes) to bytes */
   return ntb.bld.SHL(retype(offset, BRW_TYPE_UD), brw_imm_ud(4u));
}

static void
fs_nir_emit_vs_intrinsic(nir_to_brw_state &ntb,
                         nir_intrinsic_instr *instr)
{
   const fs_builder &bld = ntb.bld;
   fs_visitor &s = ntb.s;
   assert(s.stage == MESA_SHADER_VERTEX);

   brw_reg dest;
   if (nir_intrinsic_infos[instr->intrinsic].has_dest)
      dest = get_nir_def(ntb, instr->def);

   switch (instr->intrinsic) {
   case nir_intrinsic_load_vertex_id:
   case nir_intrinsic_load_base_vertex:
      unreachable("should be lowered by nir_lower_system_values()");

   case nir_intrinsic_load_input: {
      assert(instr->def.bit_size == 32);
      const brw_reg src = offset(brw_attr_reg(0, dest.type), bld,
                                nir_intrinsic_base(instr) * 4 +
                                nir_intrinsic_component(instr) +
                                nir_src_as_uint(instr->src[0]));
      brw_combine_with_vec(bld, dest, src, instr->num_components);
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
      fs_nir_emit_intrinsic(ntb, bld, instr);
      break;
   }
}

static brw_reg
get_tcs_single_patch_icp_handle(nir_to_brw_state &ntb, const fs_builder &bld,
                                nir_intrinsic_instr *instr)
{
   fs_visitor &s = ntb.s;

   struct brw_tcs_prog_data *tcs_prog_data = brw_tcs_prog_data(s.prog_data);
   const nir_src &vertex_src = instr->src[0];
   nir_intrinsic_instr *vertex_intrin = nir_src_as_intrinsic(vertex_src);

   const brw_reg start = s.tcs_payload().icp_handle_start;

   brw_reg icp_handle;

   if (nir_src_is_const(vertex_src)) {
      /* Emit a MOV to resolve <0,1,0> regioning. */
      unsigned vertex = nir_src_as_uint(vertex_src);
      icp_handle = bld.MOV(component(start, vertex));
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
      icp_handle = bld.vgrf(BRW_TYPE_UD);

      /* Each ICP handle is a single DWord (4 bytes) */
      brw_reg vertex_offset_bytes =
         bld.SHL(retype(get_nir_src(ntb, vertex_src), BRW_TYPE_UD),
                 brw_imm_ud(2u));

      /* We might read up to 4 registers. */
      bld.emit(SHADER_OPCODE_MOV_INDIRECT, icp_handle,
               start, vertex_offset_bytes,
               brw_imm_ud(4 * REG_SIZE));
   }

   return icp_handle;
}

static brw_reg
get_tcs_multi_patch_icp_handle(nir_to_brw_state &ntb, const fs_builder &bld,
                               nir_intrinsic_instr *instr)
{
   fs_visitor &s = ntb.s;
   const intel_device_info *devinfo = s.devinfo;

   struct brw_tcs_prog_key *tcs_key = (struct brw_tcs_prog_key *) s.key;
   const nir_src &vertex_src = instr->src[0];
   const unsigned grf_size_bytes = REG_SIZE * reg_unit(devinfo);

   const brw_reg start = s.tcs_payload().icp_handle_start;

   if (nir_src_is_const(vertex_src))
      return byte_offset(start, nir_src_as_uint(vertex_src) * grf_size_bytes);

   /* The vertex index is non-constant.  We need to use indirect
    * addressing to fetch the proper URB handle.
    *
    * First, we start with the sequence indicating that channel <n>
    * should read the handle from DWord <n>.  We convert that to bytes
    * by multiplying by 4.
    *
    * Next, we convert the vertex index to bytes by multiplying
    * by the GRF size (by shifting), and add the two together.  This is
    * the final indirect byte offset.
    */
   brw_reg sequence = bld.LOAD_SUBGROUP_INVOCATION();

   /* Offsets will be 0, 4, 8, ... */
   brw_reg channel_offsets = bld.SHL(sequence, brw_imm_ud(2u));
   /* Convert vertex_index to bytes (multiply by 32) */
   assert(util_is_power_of_two_nonzero(grf_size_bytes)); /* for ffs() */
   brw_reg vertex_offset_bytes =
      bld.SHL(retype(get_nir_src(ntb, vertex_src), BRW_TYPE_UD),
              brw_imm_ud(ffs(grf_size_bytes) - 1));
   brw_reg icp_offset_bytes =
      bld.ADD(vertex_offset_bytes, channel_offsets);

   /* Use start of ICP handles as the base offset.  There is one register
    * of URB handles per vertex, so inform the register allocator that
    * we might read up to nir->info.gs.vertices_in registers.
    */
   brw_reg icp_handle = bld.vgrf(BRW_TYPE_UD);
   bld.emit(SHADER_OPCODE_MOV_INDIRECT, icp_handle, start,
            icp_offset_bytes,
            brw_imm_ud(brw_tcs_prog_key_input_vertices(tcs_key) *
                       grf_size_bytes));

   return icp_handle;
}

static void
setup_barrier_message_payload_gfx125(const fs_builder &bld,
                                     const brw_reg &msg_payload)
{
   const fs_builder ubld = bld.exec_all().group(1, 0);
   const struct intel_device_info *devinfo = bld.shader->devinfo;
   assert(devinfo->verx10 >= 125);

   /* From BSpec: 54006, mov r0.2[31:24] into m0.2[31:24] and m0.2[23:16] */
   brw_reg m0_10ub = horiz_offset(retype(msg_payload, BRW_TYPE_UB), 10);
   brw_reg r0_11ub =
      stride(suboffset(retype(brw_vec1_grf(0, 0), BRW_TYPE_UB), 11),
             0, 1, 0);
   ubld.group(2, 0).MOV(m0_10ub, r0_11ub);

   if (devinfo->ver >= 20) {
      /* Use an active threads barrier. */
      const brw_reg m0_2ud = component(retype(msg_payload, BRW_TYPE_UD), 2);
      ubld.OR(m0_2ud, m0_2ud, brw_imm_ud(1u << 8));
   }
}

static void
emit_barrier(nir_to_brw_state &ntb)
{
   const intel_device_info *devinfo = ntb.devinfo;
   const fs_builder &bld = ntb.bld;
   const fs_builder ubld = bld.exec_all();
   const fs_builder hbld = ubld.group(8 * reg_unit(devinfo), 0);
   fs_visitor &s = ntb.s;

   /* We are getting the barrier ID from the compute shader header */
   assert(gl_shader_stage_uses_workgroup(s.stage));

   /* Zero-initialize the payload */
   brw_reg payload = hbld.MOV(brw_imm_ud(0u));

   if (devinfo->verx10 >= 125) {
      setup_barrier_message_payload_gfx125(bld, payload);
   } else {
      assert(gl_shader_stage_is_compute(s.stage));

      brw_reg barrier_id_mask =
         brw_imm_ud(devinfo->ver == 9 ? 0x8f000000u : 0x7f000000u);

      /* Copy the barrier id from r0.2 to the message payload reg.2 */
      brw_reg r0_2 = brw_reg(retype(brw_vec1_grf(0, 2), BRW_TYPE_UD));
      ubld.group(1, 0).AND(component(payload, 2), r0_2, barrier_id_mask);
   }

   /* Emit a gateway "barrier" message using the payload we set up, followed
    * by a wait instruction.
    */
   ubld.emit(SHADER_OPCODE_BARRIER, reg_undef, payload);
}

static void
emit_tcs_barrier(nir_to_brw_state &ntb)
{
   const intel_device_info *devinfo = ntb.devinfo;
   const fs_builder &bld = ntb.bld;
   fs_visitor &s = ntb.s;

   assert(s.stage == MESA_SHADER_TESS_CTRL);
   struct brw_tcs_prog_data *tcs_prog_data = brw_tcs_prog_data(s.prog_data);

   brw_reg m0 = bld.vgrf(BRW_TYPE_UD);
   brw_reg m0_2 = component(m0, 2);

   const fs_builder chanbld = bld.exec_all().group(1, 0);

   /* Zero the message header */
   bld.exec_all().MOV(m0, brw_imm_ud(0u));

   if (devinfo->verx10 >= 125) {
      setup_barrier_message_payload_gfx125(bld, m0);
   } else if (devinfo->ver >= 11) {
      chanbld.AND(m0_2, retype(brw_vec1_grf(0, 2), BRW_TYPE_UD),
                  brw_imm_ud(INTEL_MASK(30, 24)));

      /* Set the Barrier Count and the enable bit */
      chanbld.OR(m0_2, m0_2,
                 brw_imm_ud(tcs_prog_data->instances << 8 | (1 << 15)));
   } else {
      /* Copy "Barrier ID" from r0.2, bits 16:13 */
      chanbld.AND(m0_2, retype(brw_vec1_grf(0, 2), BRW_TYPE_UD),
                  brw_imm_ud(INTEL_MASK(16, 13)));

      /* Shift it up to bits 27:24. */
      chanbld.SHL(m0_2, m0_2, brw_imm_ud(11));

      /* Set the Barrier Count and the enable bit */
      chanbld.OR(m0_2, m0_2,
                 brw_imm_ud(tcs_prog_data->instances << 9 | (1 << 15)));
   }

   bld.emit(SHADER_OPCODE_BARRIER, bld.null_reg_ud(), m0);
}

static void
fs_nir_emit_tcs_intrinsic(nir_to_brw_state &ntb,
                          nir_intrinsic_instr *instr)
{
   const intel_device_info *devinfo = ntb.devinfo;
   const fs_builder &bld = ntb.bld;
   fs_visitor &s = ntb.s;

   assert(s.stage == MESA_SHADER_TESS_CTRL);
   struct brw_tcs_prog_data *tcs_prog_data = brw_tcs_prog_data(s.prog_data);
   struct brw_vue_prog_data *vue_prog_data = &tcs_prog_data->base;

   brw_reg dst;
   if (nir_intrinsic_infos[instr->intrinsic].has_dest)
      dst = get_nir_def(ntb, instr->def);

   switch (instr->intrinsic) {
   case nir_intrinsic_load_primitive_id:
      bld.MOV(dst, s.tcs_payload().primitive_id);
      break;
   case nir_intrinsic_load_invocation_id:
      bld.MOV(retype(dst, s.invocation_id.type), s.invocation_id);
      break;

   case nir_intrinsic_barrier:
      if (nir_intrinsic_memory_scope(instr) != SCOPE_NONE)
         fs_nir_emit_intrinsic(ntb, bld, instr);
      if (nir_intrinsic_execution_scope(instr) == SCOPE_WORKGROUP) {
         if (tcs_prog_data->instances != 1)
            emit_tcs_barrier(ntb);
      }
      break;

   case nir_intrinsic_load_input:
      unreachable("nir_lower_io should never give us these.");
      break;

   case nir_intrinsic_load_per_vertex_input: {
      assert(instr->def.bit_size == 32);
      brw_reg indirect_offset = get_indirect_offset(ntb, instr);
      unsigned imm_offset = nir_intrinsic_base(instr);
      fs_inst *inst;

      const bool multi_patch =
         vue_prog_data->dispatch_mode == INTEL_DISPATCH_MODE_TCS_MULTI_PATCH;

      brw_reg icp_handle = multi_patch ?
         get_tcs_multi_patch_icp_handle(ntb, bld, instr) :
         get_tcs_single_patch_icp_handle(ntb, bld, instr);

      /* We can only read two double components with each URB read, so
       * we send two read messages in that case, each one loading up to
       * two double components.
       */
      unsigned num_components = instr->num_components;
      unsigned first_component = nir_intrinsic_component(instr);

      brw_reg srcs[URB_LOGICAL_NUM_SRCS];
      srcs[URB_LOGICAL_SRC_HANDLE] = icp_handle;

      if (indirect_offset.file == BAD_FILE) {
         /* Constant indexing - use global offset. */
         if (first_component != 0) {
            unsigned read_components = num_components + first_component;
            brw_reg tmp = bld.vgrf(dst.type, read_components);
            inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, tmp, srcs,
                            ARRAY_SIZE(srcs));
            brw_combine_with_vec(bld, dst, offset(tmp, bld, first_component),
                                 num_components);
         } else {
            inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, dst, srcs,
                            ARRAY_SIZE(srcs));
         }
         inst->offset = imm_offset;
      } else {
         /* Indirect indexing - use per-slot offsets as well. */
         srcs[URB_LOGICAL_SRC_PER_SLOT_OFFSETS] = indirect_offset;

         if (first_component != 0) {
            unsigned read_components = num_components + first_component;
            brw_reg tmp = bld.vgrf(dst.type, read_components);
            inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, tmp,
                            srcs, ARRAY_SIZE(srcs));
            brw_combine_with_vec(bld, dst, offset(tmp, bld, first_component),
                                 num_components);
         } else {
            inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, dst,
                            srcs, ARRAY_SIZE(srcs));
         }
         inst->offset = imm_offset;
      }
      inst->size_written = (num_components + first_component) *
                           inst->dst.component_size(inst->exec_size);

      /* Copy the temporary to the destination to deal with writemasking.
       *
       * Also attempt to deal with gl_PointSize being in the .w component.
       */
      if (inst->offset == 0 && indirect_offset.file == BAD_FILE) {
         assert(brw_type_size_bytes(dst.type) == 4);
         inst->dst = bld.vgrf(dst.type, 4);
         inst->size_written = 4 * REG_SIZE * reg_unit(devinfo);
         bld.MOV(dst, offset(inst->dst, bld, 3));
      }
      break;
   }

   case nir_intrinsic_load_output:
   case nir_intrinsic_load_per_vertex_output: {
      assert(instr->def.bit_size == 32);
      brw_reg indirect_offset = get_indirect_offset(ntb, instr);
      unsigned imm_offset = nir_intrinsic_base(instr);
      unsigned first_component = nir_intrinsic_component(instr);

      fs_inst *inst;
      if (indirect_offset.file == BAD_FILE) {
         /* This MOV replicates the output handle to all enabled channels
          * is SINGLE_PATCH mode.
          */
         brw_reg patch_handle = bld.MOV(s.tcs_payload().patch_urb_output);

         {
            brw_reg srcs[URB_LOGICAL_NUM_SRCS];
            srcs[URB_LOGICAL_SRC_HANDLE] = patch_handle;

            if (first_component != 0) {
               unsigned read_components =
                  instr->num_components + first_component;
               brw_reg tmp = bld.vgrf(dst.type, read_components);
               inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, tmp,
                               srcs, ARRAY_SIZE(srcs));
               inst->size_written = read_components * REG_SIZE * reg_unit(devinfo);
               brw_combine_with_vec(bld, dst, offset(tmp, bld, first_component),
                                    instr->num_components);
            } else {
               inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, dst,
                               srcs, ARRAY_SIZE(srcs));
               inst->size_written = instr->num_components * REG_SIZE * reg_unit(devinfo);
            }
            inst->offset = imm_offset;
         }
      } else {
         /* Indirect indexing - use per-slot offsets as well. */
         brw_reg srcs[URB_LOGICAL_NUM_SRCS];
         srcs[URB_LOGICAL_SRC_HANDLE] = s.tcs_payload().patch_urb_output;
         srcs[URB_LOGICAL_SRC_PER_SLOT_OFFSETS] = indirect_offset;

         if (first_component != 0) {
            unsigned read_components =
               instr->num_components + first_component;
            brw_reg tmp = bld.vgrf(dst.type, read_components);
            inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, tmp,
                            srcs, ARRAY_SIZE(srcs));
            inst->size_written = read_components * REG_SIZE * reg_unit(devinfo);
            brw_combine_with_vec(bld, dst, offset(tmp, bld, first_component),
                                 instr->num_components);
         } else {
            inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, dst,
                            srcs, ARRAY_SIZE(srcs));
            inst->size_written = instr->num_components * REG_SIZE * reg_unit(devinfo);
         }
         inst->offset = imm_offset;
      }
      break;
   }

   case nir_intrinsic_store_output:
   case nir_intrinsic_store_per_vertex_output: {
      assert(nir_src_bit_size(instr->src[0]) == 32);
      brw_reg value = get_nir_src(ntb, instr->src[0]);
      brw_reg indirect_offset = get_indirect_offset(ntb, instr);
      unsigned imm_offset = nir_intrinsic_base(instr);
      unsigned mask = nir_intrinsic_write_mask(instr);

      if (mask == 0)
         break;

      unsigned num_components = util_last_bit(mask);
      unsigned first_component = nir_intrinsic_component(instr);
      assert((first_component + num_components) <= 4);

      mask = mask << first_component;

      const bool has_urb_lsc = devinfo->ver >= 20;

      brw_reg mask_reg;
      if (mask != WRITEMASK_XYZW)
         mask_reg = brw_imm_ud(mask << 16);

      brw_reg sources[4];

      unsigned m = has_urb_lsc ? 0 : first_component;
      for (unsigned i = 0; i < num_components; i++) {
         int c = i + first_component;
         if (mask & (1 << c)) {
            sources[m++] = offset(value, bld, i);
         } else if (devinfo->ver < 20) {
            m++;
         }
      }

      assert(has_urb_lsc || m == (first_component + num_components));

      brw_reg srcs[URB_LOGICAL_NUM_SRCS];
      srcs[URB_LOGICAL_SRC_HANDLE] = s.tcs_payload().patch_urb_output;
      srcs[URB_LOGICAL_SRC_PER_SLOT_OFFSETS] = indirect_offset;
      srcs[URB_LOGICAL_SRC_CHANNEL_MASK] = mask_reg;
      srcs[URB_LOGICAL_SRC_DATA] = bld.vgrf(BRW_TYPE_F, m);
      srcs[URB_LOGICAL_SRC_COMPONENTS] = brw_imm_ud(m);
      bld.LOAD_PAYLOAD(srcs[URB_LOGICAL_SRC_DATA], sources, m, 0);

      fs_inst *inst = bld.emit(SHADER_OPCODE_URB_WRITE_LOGICAL, reg_undef,
                               srcs, ARRAY_SIZE(srcs));
      inst->offset = imm_offset;
      break;
   }

   default:
      fs_nir_emit_intrinsic(ntb, bld, instr);
      break;
   }
}

static void
fs_nir_emit_tes_intrinsic(nir_to_brw_state &ntb,
                          nir_intrinsic_instr *instr)
{
   const intel_device_info *devinfo = ntb.devinfo;
   const fs_builder &bld = ntb.bld;
   fs_visitor &s = ntb.s;

   assert(s.stage == MESA_SHADER_TESS_EVAL);
   struct brw_tes_prog_data *tes_prog_data = brw_tes_prog_data(s.prog_data);

   brw_reg dest;
   if (nir_intrinsic_infos[instr->intrinsic].has_dest)
      dest = get_nir_def(ntb, instr->def);

   switch (instr->intrinsic) {
   case nir_intrinsic_load_primitive_id:
      bld.MOV(dest, s.tes_payload().primitive_id);
      break;

   case nir_intrinsic_load_tess_coord:
      for (unsigned i = 0; i < 3; i++)
         bld.MOV(offset(dest, bld, i), s.tes_payload().coords[i]);
      break;

   case nir_intrinsic_load_input:
   case nir_intrinsic_load_per_vertex_input: {
      assert(instr->def.bit_size == 32);
      brw_reg indirect_offset = get_indirect_offset(ntb, instr);
      unsigned imm_offset = nir_intrinsic_base(instr);
      unsigned first_component = nir_intrinsic_component(instr);

      fs_inst *inst;
      if (indirect_offset.file == BAD_FILE) {
         /* Arbitrarily only push up to 32 vec4 slots worth of data,
          * which is 16 registers (since each holds 2 vec4 slots).
          */
         const unsigned max_push_slots = 32;
         if (imm_offset < max_push_slots) {
            const brw_reg src = horiz_offset(brw_attr_reg(0, dest.type),
                                            4 * imm_offset + first_component);
            brw_reg comps[NIR_MAX_VEC_COMPONENTS];
            for (unsigned i = 0; i < instr->num_components; i++) {
               comps[i] = component(src, i);
            }
            bld.VEC(dest, comps, instr->num_components);

            tes_prog_data->base.urb_read_length =
               MAX2(tes_prog_data->base.urb_read_length,
                    (imm_offset / 2) + 1);
         } else {
            /* Replicate the patch handle to all enabled channels */
            brw_reg srcs[URB_LOGICAL_NUM_SRCS];
            srcs[URB_LOGICAL_SRC_HANDLE] = s.tes_payload().patch_urb_input;

            if (first_component != 0) {
               unsigned read_components =
                  instr->num_components + first_component;
               brw_reg tmp = bld.vgrf(dest.type, read_components);
               inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, tmp,
                               srcs, ARRAY_SIZE(srcs));
               inst->size_written = read_components * REG_SIZE * reg_unit(devinfo);
               brw_combine_with_vec(bld, dest, offset(tmp, bld, first_component),
                                    instr->num_components);
            } else {
               inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, dest,
                               srcs, ARRAY_SIZE(srcs));
               inst->size_written = instr->num_components * REG_SIZE * reg_unit(devinfo);
            }
            inst->offset = imm_offset;
         }
      } else {
         /* Indirect indexing - use per-slot offsets as well. */

         /* We can only read two double components with each URB read, so
          * we send two read messages in that case, each one loading up to
          * two double components.
          */
         unsigned num_components = instr->num_components;

         brw_reg srcs[URB_LOGICAL_NUM_SRCS];
         srcs[URB_LOGICAL_SRC_HANDLE] = s.tes_payload().patch_urb_input;
         srcs[URB_LOGICAL_SRC_PER_SLOT_OFFSETS] = indirect_offset;

         if (first_component != 0) {
            unsigned read_components =
                num_components + first_component;
            brw_reg tmp = bld.vgrf(dest.type, read_components);
            inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, tmp,
                            srcs, ARRAY_SIZE(srcs));
            brw_combine_with_vec(bld, dest, offset(tmp, bld, first_component),
                                 num_components);
         } else {
            inst = bld.emit(SHADER_OPCODE_URB_READ_LOGICAL, dest,
                            srcs, ARRAY_SIZE(srcs));
         }
         inst->offset = imm_offset;
         inst->size_written = (num_components + first_component) *
                              inst->dst.component_size(inst->exec_size);
      }
      break;
   }
   default:
      fs_nir_emit_intrinsic(ntb, bld, instr);
      break;
   }
}

static void
fs_nir_emit_gs_intrinsic(nir_to_brw_state &ntb,
                         nir_intrinsic_instr *instr)
{
   const fs_builder &bld = ntb.bld;
   fs_visitor &s = ntb.s;

   assert(s.stage == MESA_SHADER_GEOMETRY);

   brw_reg dest;
   if (nir_intrinsic_infos[instr->intrinsic].has_dest)
      dest = get_nir_def(ntb, instr->def);

   switch (instr->intrinsic) {
   case nir_intrinsic_load_primitive_id:
      assert(s.stage == MESA_SHADER_GEOMETRY);
      assert(brw_gs_prog_data(s.prog_data)->include_primitive_id);
      bld.MOV(retype(dest, BRW_TYPE_UD), s.gs_payload().primitive_id);
      break;

   case nir_intrinsic_load_input:
      unreachable("load_input intrinsics are invalid for the GS stage");

   case nir_intrinsic_load_per_vertex_input:
      emit_gs_input_load(ntb, dest, instr->src[0], nir_intrinsic_base(instr),
                         instr->src[1], instr->num_components,
                         nir_intrinsic_component(instr));
      break;

   case nir_intrinsic_emit_vertex_with_counter:
      emit_gs_vertex(ntb, instr->src[0], nir_intrinsic_stream_id(instr));

      /* After an EmitVertex() call, the values of all outputs are undefined.
       * If this is not in control flow, recreate a fresh set of output
       * registers to keep their live ranges separate.
       */
      if (instr->instr.block->cf_node.parent->type == nir_cf_node_function)
         fs_nir_setup_outputs(ntb);
      break;

   case nir_intrinsic_end_primitive_with_counter:
      emit_gs_end_primitive(ntb, instr->src[0]);
      break;

   case nir_intrinsic_set_vertex_and_primitive_count:
      bld.MOV(s.final_gs_vertex_count, get_nir_src(ntb, instr->src[0]));
      break;

   case nir_intrinsic_load_invocation_id: {
      brw_reg val = ntb.system_values[SYSTEM_VALUE_INVOCATION_ID];
      assert(val.file != BAD_FILE);
      dest.type = val.type;
      bld.MOV(dest, val);
      break;
   }

   default:
      fs_nir_emit_intrinsic(ntb, bld, instr);
      break;
   }
}

/**
 * Fetch the current render target layer index.
 */
static brw_reg
fetch_render_target_array_index(const fs_builder &bld)
{
   const fs_visitor *v = bld.shader;

   if (bld.shader->devinfo->ver >= 20) {
      /* Gfx20+ has separate Render Target Array indices for each pair
       * of subspans in order to support multiple polygons, so we need
       * to use a <1;8,0> region in order to select the correct word
       * for each channel.
       */
      const brw_reg idx = bld.vgrf(BRW_TYPE_UD);

      for (unsigned i = 0; i < DIV_ROUND_UP(bld.dispatch_width(), 16); i++) {
         const fs_builder hbld = bld.group(16, i);
         const struct brw_reg reg = retype(brw_vec1_grf(2 * i + 1, 1),
                                           BRW_TYPE_UW);
         hbld.AND(offset(idx, hbld, i), stride(reg, 1, 8, 0),
                  brw_imm_uw(0x7ff));
      }

      return idx;
   } else if (bld.shader->devinfo->ver >= 12 && v->max_polygons == 2) {
      /* According to the BSpec "PS Thread Payload for Normal
       * Dispatch", the render target array index is stored as bits
       * 26:16 of either the R1.1 or R1.6 poly info dwords, for the
       * first and second polygons respectively in multipolygon PS
       * dispatch mode.
       */
      assert(bld.dispatch_width() == 16);
      const brw_reg idx = bld.vgrf(BRW_TYPE_UD);

      for (unsigned i = 0; i < v->max_polygons; i++) {
         const fs_builder hbld = bld.group(8, i);
         const struct brw_reg g1 = brw_uw1_reg(FIXED_GRF, 1, 3 + 10 * i);
         hbld.AND(offset(idx, hbld, i), g1, brw_imm_uw(0x7ff));
      }

      return idx;
   } else if (bld.shader->devinfo->ver >= 12) {
      /* The render target array index is provided in the thread payload as
       * bits 26:16 of r1.1.
       */
      const brw_reg idx = bld.vgrf(BRW_TYPE_UD);
      bld.AND(idx, brw_uw1_reg(FIXED_GRF, 1, 3),
              brw_imm_uw(0x7ff));
      return idx;
   } else {
      /* The render target array index is provided in the thread payload as
       * bits 26:16 of r0.0.
       */
      const brw_reg idx = bld.vgrf(BRW_TYPE_UD);
      bld.AND(idx, brw_uw1_reg(FIXED_GRF, 0, 1),
              brw_imm_uw(0x7ff));
      return idx;
   }
}

static brw_reg
fetch_viewport_index(const fs_builder &bld)
{
   const fs_visitor *v = bld.shader;

   if (bld.shader->devinfo->ver >= 20) {
      /* Gfx20+ has separate viewport indices for each pair
       * of subspans in order to support multiple polygons, so we need
       * to use a <1;8,0> region in order to select the correct word
       * for each channel.
       */
      const brw_reg idx = bld.vgrf(BRW_TYPE_UD);

      for (unsigned i = 0; i < DIV_ROUND_UP(bld.dispatch_width(), 16); i++) {
         const fs_builder hbld = bld.group(16, i);
         const struct brw_reg reg = retype(xe2_vec1_grf(i, 9),
                                           BRW_TYPE_UW);
         hbld.AND(offset(idx, hbld, i), stride(reg, 1, 8, 0),
                  brw_imm_uw(0xf000));
      }

      bld.SHR(idx, idx, brw_imm_ud(12));
      return idx;
   } else if (bld.shader->devinfo->ver >= 12 && v->max_polygons == 2) {
      /* According to the BSpec "PS Thread Payload for Normal
       * Dispatch", the viewport index is stored as bits
       * 30:27 of either the R1.1 or R1.6 poly info dwords, for the
       * first and second polygons respectively in multipolygon PS
       * dispatch mode.
       */
      assert(bld.dispatch_width() == 16);
      const brw_reg idx = bld.vgrf(BRW_TYPE_UD);
      brw_reg vp_idx_per_poly_dw[2] = {
         brw_ud1_reg(FIXED_GRF, 1, 1), /* R1.1 bits 30:27 */
         brw_ud1_reg(FIXED_GRF, 1, 6), /* R1.6 bits 30:27 */
      };

      for (unsigned i = 0; i < v->max_polygons; i++) {
         const fs_builder hbld = bld.group(8, i);
         hbld.SHR(offset(idx, hbld, i), vp_idx_per_poly_dw[i], brw_imm_ud(27));
      }

      return bld.AND(idx, brw_imm_ud(0xf));
   } else if (bld.shader->devinfo->ver >= 12) {
      /* The viewport index is provided in the thread payload as
       * bits 30:27 of r1.1.
       */
      const brw_reg idx = bld.vgrf(BRW_TYPE_UD);
      bld.SHR(idx,
              bld.AND(brw_uw1_reg(FIXED_GRF, 1, 3),
                      brw_imm_uw(0x7800)),
              brw_imm_ud(11));
      return idx;
   } else {
      /* The viewport index is provided in the thread payload as
       * bits 30:27 of r0.0.
       */
      const brw_reg idx = bld.vgrf(BRW_TYPE_UD);
      bld.SHR(idx,
              bld.AND(brw_uw1_reg(FIXED_GRF, 0, 1),
                      brw_imm_uw(0x7800)),
              brw_imm_ud(11));
      return idx;
   }
}

/* Sample from the MCS surface attached to this multisample texture. */
static brw_reg
emit_mcs_fetch(nir_to_brw_state &ntb, const brw_reg &coordinate, unsigned components,
               const brw_reg &texture,
               const brw_reg &texture_handle)
{
   const fs_builder &bld = ntb.bld;

   const brw_reg dest = bld.vgrf(BRW_TYPE_UD, 4);

   brw_reg srcs[TEX_LOGICAL_NUM_SRCS];
   srcs[TEX_LOGICAL_SRC_COORDINATE] = coordinate;
   srcs[TEX_LOGICAL_SRC_SURFACE] = texture;
   srcs[TEX_LOGICAL_SRC_SAMPLER] = brw_imm_ud(0);
   srcs[TEX_LOGICAL_SRC_SURFACE_HANDLE] = texture_handle;
   srcs[TEX_LOGICAL_SRC_COORD_COMPONENTS] = brw_imm_d(components);
   srcs[TEX_LOGICAL_SRC_GRAD_COMPONENTS] = brw_imm_d(0);
   srcs[TEX_LOGICAL_SRC_RESIDENCY] = brw_imm_d(0);

   fs_inst *inst = bld.emit(SHADER_OPCODE_TXF_MCS_LOGICAL, dest, srcs,
                            ARRAY_SIZE(srcs));

   /* We only care about one or two regs of response, but the sampler always
    * writes 4/8.
    */
   inst->size_written = 4 * dest.component_size(inst->exec_size);

   return dest;
}

/**
 * Fake non-coherent framebuffer read implemented using TXF to fetch from the
 * framebuffer at the current fragment coordinates and sample index.
 */
static fs_inst *
emit_non_coherent_fb_read(nir_to_brw_state &ntb, const fs_builder &bld, const brw_reg &dst,
                          unsigned target)
{
   fs_visitor &s = ntb.s;
   const struct intel_device_info *devinfo = s.devinfo;

   assert(bld.shader->stage == MESA_SHADER_FRAGMENT);
   const brw_wm_prog_key *wm_key =
      reinterpret_cast<const brw_wm_prog_key *>(s.key);
   assert(!wm_key->coherent_fb_fetch);

   /* Calculate the fragment coordinates. */
   const brw_reg coords = bld.vgrf(BRW_TYPE_UD, 3);
   bld.MOV(offset(coords, bld, 0), s.pixel_x);
   bld.MOV(offset(coords, bld, 1), s.pixel_y);
   bld.MOV(offset(coords, bld, 2), fetch_render_target_array_index(bld));

   /* Calculate the sample index and MCS payload when multisampling.  Luckily
    * the MCS fetch message behaves deterministically for UMS surfaces, so it
    * shouldn't be necessary to recompile based on whether the framebuffer is
    * CMS or UMS.
    */
   assert(wm_key->multisample_fbo == BRW_ALWAYS ||
          wm_key->multisample_fbo == BRW_NEVER);
   if (wm_key->multisample_fbo &&
       ntb.system_values[SYSTEM_VALUE_SAMPLE_ID].file == BAD_FILE)
      ntb.system_values[SYSTEM_VALUE_SAMPLE_ID] = emit_sampleid_setup(ntb);

   const brw_reg sample = ntb.system_values[SYSTEM_VALUE_SAMPLE_ID];
   const brw_reg mcs = wm_key->multisample_fbo ?
      emit_mcs_fetch(ntb, coords, 3, brw_imm_ud(target), brw_reg()) : brw_reg();

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
      else
         op = SHADER_OPCODE_TXF_CMS_W_LOGICAL;
   } else {
      op = SHADER_OPCODE_TXF_LOGICAL;
   }

   /* Emit the instruction. */
   brw_reg srcs[TEX_LOGICAL_NUM_SRCS];
   srcs[TEX_LOGICAL_SRC_COORDINATE]       = coords;
   srcs[TEX_LOGICAL_SRC_LOD]              = brw_imm_ud(0);
   srcs[TEX_LOGICAL_SRC_SAMPLE_INDEX]     = sample;
   srcs[TEX_LOGICAL_SRC_MCS]              = mcs;
   srcs[TEX_LOGICAL_SRC_SURFACE]          = brw_imm_ud(target);
   srcs[TEX_LOGICAL_SRC_SAMPLER]          = brw_imm_ud(0);
   srcs[TEX_LOGICAL_SRC_COORD_COMPONENTS] = brw_imm_ud(3);
   srcs[TEX_LOGICAL_SRC_GRAD_COMPONENTS]  = brw_imm_ud(0);
   srcs[TEX_LOGICAL_SRC_RESIDENCY]        = brw_imm_ud(0);

   fs_inst *inst = bld.emit(op, dst, srcs, ARRAY_SIZE(srcs));
   inst->size_written = 4 * inst->dst.component_size(inst->exec_size);

   return inst;
}

/**
 * Actual coherent framebuffer read implemented using the native render target
 * read message.  Requires SKL+.
 */
static fs_inst *
emit_coherent_fb_read(const fs_builder &bld, const brw_reg &dst, unsigned target)
{
   fs_inst *inst = bld.emit(FS_OPCODE_FB_READ_LOGICAL, dst);
   inst->target = target;
   inst->size_written = 4 * inst->dst.component_size(inst->exec_size);

   return inst;
}

static brw_reg
alloc_temporary(const fs_builder &bld, unsigned size, brw_reg *regs, unsigned n)
{
   if (n && regs[0].file != BAD_FILE) {
      return regs[0];

   } else {
      const brw_reg tmp = bld.vgrf(BRW_TYPE_F, size);

      for (unsigned i = 0; i < n; i++)
         regs[i] = tmp;

      return tmp;
   }
}

static brw_reg
alloc_frag_output(nir_to_brw_state &ntb, unsigned location)
{
   fs_visitor &s = ntb.s;

   assert(s.stage == MESA_SHADER_FRAGMENT);
   const brw_wm_prog_key *const key =
      reinterpret_cast<const brw_wm_prog_key *>(s.key);
   const unsigned l = GET_FIELD(location, BRW_NIR_FRAG_OUTPUT_LOCATION);
   const unsigned i = GET_FIELD(location, BRW_NIR_FRAG_OUTPUT_INDEX);

   if (i > 0 || (key->force_dual_color_blend && l == FRAG_RESULT_DATA1))
      return alloc_temporary(ntb.bld, 4, &s.dual_src_output, 1);

   else if (l == FRAG_RESULT_COLOR)
      return alloc_temporary(ntb.bld, 4, s.outputs,
                             MAX2(key->nr_color_regions, 1));

   else if (l == FRAG_RESULT_DEPTH)
      return alloc_temporary(ntb.bld, 1, &s.frag_depth, 1);

   else if (l == FRAG_RESULT_STENCIL)
      return alloc_temporary(ntb.bld, 1, &s.frag_stencil, 1);

   else if (l == FRAG_RESULT_SAMPLE_MASK)
      return alloc_temporary(ntb.bld, 1, &s.sample_mask, 1);

   else if (l >= FRAG_RESULT_DATA0 &&
            l < FRAG_RESULT_DATA0 + BRW_MAX_DRAW_BUFFERS)
      return alloc_temporary(ntb.bld, 4,
                             &s.outputs[l - FRAG_RESULT_DATA0], 1);

   else
      unreachable("Invalid location");
}

static void
emit_is_helper_invocation(nir_to_brw_state &ntb, brw_reg result)
{
   const fs_builder &bld = ntb.bld;

   /* Unlike the regular gl_HelperInvocation, that is defined at dispatch,
    * the helperInvocationEXT() (aka SpvOpIsHelperInvocationEXT) takes into
    * consideration demoted invocations.
    */
   result.type = BRW_TYPE_UD;

   bld.MOV(result, brw_imm_ud(0));

   /* See brw_sample_mask_reg() for why we split SIMD32 into SIMD16 here. */
   unsigned width = bld.dispatch_width();
   for (unsigned i = 0; i < DIV_ROUND_UP(width, 16); i++) {
      const fs_builder b = bld.group(MIN2(width, 16), i);

      fs_inst *mov = b.MOV(offset(result, b, i), brw_imm_ud(~0));

      /* The at() ensures that any code emitted to get the predicate happens
       * before the mov right above.  This is not an issue elsewhere because
       * lowering code already set up the builder this way.
       */
      brw_emit_predicate_on_sample_mask(b.at(NULL, mov), mov);
      mov->predicate_inverse = true;
   }
}

static brw_reg
emit_frontfacing_interpolation(nir_to_brw_state &ntb)
{
   const intel_device_info *devinfo = ntb.devinfo;
   const fs_builder &bld = ntb.bld;
   fs_visitor &s = ntb.s;

   brw_reg ff = bld.vgrf(BRW_TYPE_D);

   if (devinfo->ver >= 20) {
      /* Gfx20+ has separate back-facing bits for each pair of
       * subspans in order to support multiple polygons, so we need to
       * use a <1;8,0> region in order to select the correct word for
       * each channel.
       */
      const brw_reg tmp = bld.vgrf(BRW_TYPE_UW);

      for (unsigned i = 0; i < DIV_ROUND_UP(s.dispatch_width, 16); i++) {
         const fs_builder hbld = bld.group(16, i);
         const struct brw_reg gi_uw = retype(xe2_vec1_grf(i, 9),
                                             BRW_TYPE_UW);
         hbld.AND(offset(tmp, hbld, i), gi_uw, brw_imm_uw(0x800));
      }

      bld.CMP(ff, tmp, brw_imm_uw(0), BRW_CONDITIONAL_Z);

   } else if (devinfo->ver >= 12 && s.max_polygons == 2) {
      /* According to the BSpec "PS Thread Payload for Normal
       * Dispatch", the front/back facing interpolation bit is stored
       * as bit 15 of either the R1.1 or R1.6 poly info field, for the
       * first and second polygons respectively in multipolygon PS
       * dispatch mode.
       */
      assert(s.dispatch_width == 16);
      brw_reg tmp = bld.vgrf(BRW_TYPE_W);

      for (unsigned i = 0; i < s.max_polygons; i++) {
         const fs_builder hbld = bld.group(8, i);
         const struct brw_reg g1 = retype(brw_vec1_grf(1, 1 + 5 * i),
                                          BRW_TYPE_W);
         hbld.ASR(offset(tmp, hbld, i), g1, brw_imm_d(15));
      }

      bld.NOT(ff, tmp);

   } else if (devinfo->ver >= 12) {
      brw_reg g1 = brw_reg(retype(brw_vec1_grf(1, 1), BRW_TYPE_W));

      brw_reg tmp = bld.vgrf(BRW_TYPE_W);
      bld.ASR(tmp, g1, brw_imm_d(15));
      bld.NOT(ff, tmp);
   } else {
      /* Bit 15 of g0.0 is 0 if the polygon is front facing. We want to create
       * a boolean result from this (~0/true or 0/false).
       *
       * We can use the fact that bit 15 is the MSB of g0.0:W to accomplish
       * this task in only one instruction:
       *    - a negation source modifier will flip the bit; and
       *    - a W -> D type conversion will sign extend the bit into the high
       *      word of the destination.
       *
       * An ASR 15 fills the low word of the destination.
       */
      brw_reg g0 = brw_reg(retype(brw_vec1_grf(0, 0), BRW_TYPE_W));

      bld.ASR(ff, negate(g0), brw_imm_d(15));
   }

   return ff;
}

static brw_reg
emit_samplepos_setup(nir_to_brw_state &ntb)
{
   const fs_builder &bld = ntb.bld;
   fs_visitor &s = ntb.s;

   assert(s.stage == MESA_SHADER_FRAGMENT);
   struct brw_wm_prog_data *wm_prog_data = brw_wm_prog_data(s.prog_data);

   const fs_builder abld = bld.annotate("compute sample position");
   brw_reg pos = abld.vgrf(BRW_TYPE_F, 2);

   if (wm_prog_data->persample_dispatch == BRW_NEVER) {
      /* From ARB_sample_shading specification:
       * "When rendering to a non-multisample buffer, or if multisample
       *  rasterization is disabled, gl_SamplePosition will always be
       *  (0.5, 0.5).
       */
      bld.MOV(offset(pos, bld, 0), brw_imm_f(0.5f));
      bld.MOV(offset(pos, bld, 1), brw_imm_f(0.5f));
      return pos;
   }

   /* WM will be run in MSDISPMODE_PERSAMPLE. So, only one of SIMD8 or SIMD16
    * mode will be enabled.
    *
    * From the Ivy Bridge PRM, volume 2 part 1, page 344:
    * R31.1:0         Position Offset X/Y for Slot[3:0]
    * R31.3:2         Position Offset X/Y for Slot[7:4]
    * .....
    *
    * The X, Y sample positions come in as bytes in  thread payload. So, read
    * the positions using vstride=16, width=8, hstride=2.
    */
   const brw_reg sample_pos_reg =
      fetch_payload_reg(abld, s.fs_payload().sample_pos_reg, BRW_TYPE_W);

   for (unsigned i = 0; i < 2; i++) {
      brw_reg tmp_d = bld.vgrf(BRW_TYPE_D);
      abld.MOV(tmp_d, subscript(sample_pos_reg, BRW_TYPE_B, i));
      /* Convert int_sample_pos to floating point */
      brw_reg tmp_f = bld.vgrf(BRW_TYPE_F);
      abld.MOV(tmp_f, tmp_d);
      /* Scale to the range [0, 1] */
      abld.MUL(offset(pos, abld, i), tmp_f, brw_imm_f(1 / 16.0f));
   }

   if (wm_prog_data->persample_dispatch == BRW_SOMETIMES) {
      check_dynamic_msaa_flag(abld, wm_prog_data,
                              INTEL_MSAA_FLAG_PERSAMPLE_DISPATCH);
      for (unsigned i = 0; i < 2; i++) {
         set_predicate(BRW_PREDICATE_NORMAL,
                       bld.SEL(offset(pos, abld, i), offset(pos, abld, i),
                               brw_imm_f(0.5f)));
      }
   }

   return pos;
}

static brw_reg
emit_sampleid_setup(nir_to_brw_state &ntb)
{
   const intel_device_info *devinfo = ntb.devinfo;
   const fs_builder &bld = ntb.bld;
   fs_visitor &s = ntb.s;

   assert(s.stage == MESA_SHADER_FRAGMENT);
   ASSERTED brw_wm_prog_key *key = (brw_wm_prog_key*) s.key;
   struct brw_wm_prog_data *wm_prog_data = brw_wm_prog_data(s.prog_data);

   const fs_builder abld = bld.annotate("compute sample id");
   brw_reg sample_id = abld.vgrf(BRW_TYPE_UD);

   assert(key->multisample_fbo != BRW_NEVER);

   /* Sample ID comes in as 4-bit numbers in g1.0:
    *
    *    15:12 Slot 3 SampleID (only used in SIMD16)
    *     11:8 Slot 2 SampleID (only used in SIMD16)
    *      7:4 Slot 1 SampleID
    *      3:0 Slot 0 SampleID
    *
    * Each slot corresponds to four channels, so we want to replicate each
    * half-byte value to 4 channels in a row:
    *
    *    dst+0:    .7    .6    .5    .4    .3    .2    .1    .0
    *             7:4   7:4   7:4   7:4   3:0   3:0   3:0   3:0
    *
    *    dst+1:    .7    .6    .5    .4    .3    .2    .1    .0  (if SIMD16)
    *           15:12 15:12 15:12 15:12  11:8  11:8  11:8  11:8
    *
    * First, we read g1.0 with a <1,8,0>UB region, causing the first 8
    * channels to read the first byte (7:0), and the second group of 8
    * channels to read the second byte (15:8).  Then, we shift right by
    * a vector immediate of <4, 4, 4, 4, 0, 0, 0, 0>, moving the slot 1 / 3
    * values into place.  Finally, we AND with 0xf to keep the low nibble.
    *
    *    shr(16) tmp<1>W g1.0<1,8,0>B 0x44440000:V
    *    and(16) dst<1>D tmp<8,8,1>W  0xf:W
    *
    * TODO: These payload bits exist on Gfx7 too, but they appear to always
    *       be zero, so this code fails to work.  We should find out why.
    */
   const brw_reg tmp = abld.vgrf(BRW_TYPE_UW);

   for (unsigned i = 0; i < DIV_ROUND_UP(s.dispatch_width, 16); i++) {
      const fs_builder hbld = abld.group(MIN2(16, s.dispatch_width), i);
      /* According to the "PS Thread Payload for Normal Dispatch"
       * pages on the BSpec, the sample ids are stored in R0.8/R1.8
       * on gfx20+ and in R1.0/R2.0 on gfx8+.
       */
      const struct brw_reg id_reg = devinfo->ver >= 20 ? xe2_vec1_grf(i, 8) :
                                    brw_vec1_grf(i + 1, 0);
      hbld.SHR(offset(tmp, hbld, i),
               stride(retype(id_reg, BRW_TYPE_UB), 1, 8, 0),
               brw_imm_v(0x44440000));
   }

   abld.AND(sample_id, tmp, brw_imm_w(0xf));

   if (key->multisample_fbo == BRW_SOMETIMES) {
      check_dynamic_msaa_flag(abld, wm_prog_data,
                              INTEL_MSAA_FLAG_MULTISAMPLE_FBO);
      set_predicate(BRW_PREDICATE_NORMAL,
                    abld.SEL(sample_id, sample_id, brw_imm_ud(0)));
   }

   return sample_id;
}

static brw_reg
emit_samplemaskin_setup(nir_to_brw_state &ntb)
{
   const fs_builder &bld = ntb.bld;
   fs_visitor &s = ntb.s;

   assert(s.stage == MESA_SHADER_FRAGMENT);
   struct brw_wm_prog_data *wm_prog_data = brw_wm_prog_data(s.prog_data);

   /* The HW doesn't provide us with expected values. */
   assert(wm_prog_data->coarse_pixel_dispatch != BRW_ALWAYS);

   brw_reg coverage_mask =
      fetch_payload_reg(bld, s.fs_payload().sample_mask_in_reg, BRW_TYPE_UD);

   if (wm_prog_data->persample_dispatch == BRW_NEVER)
      return coverage_mask;

   /* gl_SampleMaskIn[] comes from two sources: the input coverage mask,
    * and a mask representing which sample is being processed by the
    * current shader invocation.
    *
    * From the OES_sample_variables specification:
    * "When per-sample shading is active due to the use of a fragment input
    *  qualified by "sample" or due to the use of the gl_SampleID or
    *  gl_SamplePosition variables, only the bit for the current sample is
    *  set in gl_SampleMaskIn."
    */
   const fs_builder abld = bld.annotate("compute gl_SampleMaskIn");

   if (ntb.system_values[SYSTEM_VALUE_SAMPLE_ID].file == BAD_FILE)
      ntb.system_values[SYSTEM_VALUE_SAMPLE_ID] = emit_sampleid_setup(ntb);

   brw_reg one = abld.MOV(brw_imm_ud(1));
   brw_reg enabled_mask = abld.SHL(one, ntb.system_values[SYSTEM_VALUE_SAMPLE_ID]);
   brw_reg mask = abld.AND(enabled_mask, coverage_mask);

   if (wm_prog_data->persample_dispatch == BRW_ALWAYS)
      return mask;

   check_dynamic_msaa_flag(abld, wm_prog_data,
                           INTEL_MSAA_FLAG_PERSAMPLE_DISPATCH);
   set_predicate(BRW_PREDICATE_NORMAL, abld.SEL(mask, mask, coverage_mask));

   return mask;
}

static brw_reg
emit_shading_rate_setup(nir_to_brw_state &ntb)
{
   const intel_device_info *devinfo = ntb.devinfo;
   const fs_builder &bld = ntb.bld;

   assert(devinfo->ver >= 11);

   struct brw_wm_prog_data *wm_prog_data =
      brw_wm_prog_data(bld.shader->prog_data);

   /* Coarse pixel shading size fields overlap with other fields of not in
    * coarse pixel dispatch mode, so report 0 when that's not the case.
    */
   if (wm_prog_data->coarse_pixel_dispatch == BRW_NEVER)
      return brw_imm_ud(0);

   const fs_builder abld = bld.annotate("compute fragment shading rate");

   /* The shading rates provided in the shader are the actual 2D shading
    * rate while the SPIR-V built-in is the enum value that has the shading
    * rate encoded as a bitfield.  Fortunately, the bitfield value is just
    * the shading rate divided by two and shifted.
    */

   /* r1.0 - 0:7 ActualCoarsePixelShadingSize.X */
   brw_reg actual_x = brw_reg(retype(brw_vec1_grf(1, 0), BRW_TYPE_UB));
   /* r1.0 - 15:8 ActualCoarsePixelShadingSize.Y */
   brw_reg actual_y = byte_offset(actual_x, 1);

   brw_reg int_rate_y = abld.SHR(actual_y, brw_imm_ud(1));
   brw_reg int_rate_x = abld.SHR(actual_x, brw_imm_ud(1));

   brw_reg rate = abld.OR(abld.SHL(int_rate_x, brw_imm_ud(2)), int_rate_y);

   if (wm_prog_data->coarse_pixel_dispatch == BRW_ALWAYS)
      return rate;

   check_dynamic_msaa_flag(abld, wm_prog_data,
                           INTEL_MSAA_FLAG_COARSE_RT_WRITES);
   set_predicate(BRW_PREDICATE_NORMAL, abld.SEL(rate, rate, brw_imm_ud(0)));

   return rate;
}

/* Input data is organized with first the per-primitive values, followed
 * by per-vertex values.  The per-vertex will have interpolation information
 * associated, so use 4 components for each value.
 */

/* The register location here is relative to the start of the URB
 * data.  It will get adjusted to be a real location before
 * generate_code() time.
 */
static brw_reg
brw_interp_reg(const fs_builder &bld, unsigned location,
               unsigned channel, unsigned comp)
{
   fs_visitor &s = *bld.shader;
   assert(s.stage == MESA_SHADER_FRAGMENT);
   assert(BITFIELD64_BIT(location) & ~s.nir->info.per_primitive_inputs);

   const struct brw_wm_prog_data *prog_data = brw_wm_prog_data(s.prog_data);

   assert(prog_data->urb_setup[location] >= 0);
   unsigned nr = prog_data->urb_setup[location];
   channel += prog_data->urb_setup_channel[location];

   /* Adjust so we start counting from the first per_vertex input. */
   assert(nr >= prog_data->num_per_primitive_inputs);
   nr -= prog_data->num_per_primitive_inputs;

   const unsigned per_vertex_start = prog_data->num_per_primitive_inputs;
   const unsigned regnr = per_vertex_start + (nr * 4) + channel;

   if (s.max_polygons > 1) {
      /* In multipolygon dispatch each plane parameter is a
       * dispatch_width-wide SIMD vector (see comment in
       * assign_urb_setup()), so we need to use offset() instead of
       * component() to select the specified parameter.
       */
      const brw_reg tmp = bld.vgrf(BRW_TYPE_UD);
      bld.MOV(tmp, offset(brw_attr_reg(regnr, BRW_TYPE_UD),
                          s.dispatch_width, comp));
      return retype(tmp, BRW_TYPE_F);
   } else {
      return component(brw_attr_reg(regnr, BRW_TYPE_F), comp);
   }
}

/* The register location here is relative to the start of the URB
 * data.  It will get adjusted to be a real location before
 * generate_code() time.
 */
static brw_reg
brw_per_primitive_reg(const fs_builder &bld, int location, unsigned comp)
{
   fs_visitor &s = *bld.shader;
   assert(s.stage == MESA_SHADER_FRAGMENT);
   assert(BITFIELD64_BIT(location) & s.nir->info.per_primitive_inputs);

   const struct brw_wm_prog_data *prog_data = brw_wm_prog_data(s.prog_data);

   comp += prog_data->urb_setup_channel[location];

   assert(prog_data->urb_setup[location] >= 0);

   const unsigned regnr = prog_data->urb_setup[location] + comp / 4;

   assert(regnr < prog_data->num_per_primitive_inputs);

   if (s.max_polygons > 1) {
      /* In multipolygon dispatch each primitive constant is a
       * dispatch_width-wide SIMD vector (see comment in
       * assign_urb_setup()), so we need to use offset() instead of
       * component() to select the specified parameter.
       */
      const brw_reg tmp = bld.vgrf(BRW_TYPE_UD);
      bld.MOV(tmp, offset(brw_attr_reg(regnr, BRW_TYPE_UD),
                          s.dispatch_width, comp % 4));
      return retype(tmp, BRW_TYPE_F);
   } else {
      return component(brw_attr_reg(regnr, BRW_TYPE_F), comp % 4);
   }
}

static void
fs_nir_emit_fs_intrinsic(nir_to_brw_state &ntb,
                         nir_intrinsic_instr *instr)
{
   const intel_device_info *devinfo = ntb.devinfo;
   const fs_builder &bld = ntb.bld;
   fs_visitor &s = ntb.s;

   assert(s.stage == MESA_SHADER_FRAGMENT);

   brw_reg dest;
   if (nir_intrinsic_infos[instr->intrinsic].has_dest)
      dest = get_nir_def(ntb, instr->def);

   switch (instr->intrinsic) {
   case nir_intrinsic_load_front_face:
      bld.MOV(retype(dest, BRW_TYPE_D), emit_frontfacing_interpolation(ntb));
      break;

   case nir_intrinsic_load_sample_pos:
   case nir_intrinsic_load_sample_pos_or_center: {
      brw_reg sample_pos = ntb.system_values[SYSTEM_VALUE_SAMPLE_POS];
      assert(sample_pos.file != BAD_FILE);
      dest.type = sample_pos.type;
      bld.MOV(dest, sample_pos);
      bld.MOV(offset(dest, bld, 1), offset(sample_pos, bld, 1));
      break;
   }

   case nir_intrinsic_load_layer_id:
      dest.type = BRW_TYPE_UD;
      bld.MOV(dest, fetch_render_target_array_index(bld));
      break;

   case nir_intrinsic_is_helper_invocation:
      emit_is_helper_invocation(ntb, dest);
      break;

   case nir_intrinsic_load_helper_invocation:
   case nir_intrinsic_load_sample_mask_in:
   case nir_intrinsic_load_sample_id:
   case nir_intrinsic_load_frag_shading_rate: {
      gl_system_value sv = nir_system_value_from_intrinsic(instr->intrinsic);
      brw_reg val = ntb.system_values[sv];
      assert(val.file != BAD_FILE);
      dest.type = val.type;
      bld.MOV(dest, val);
      break;
   }

   case nir_intrinsic_store_output: {
      const brw_reg src = get_nir_src(ntb, instr->src[0]);
      const unsigned store_offset = nir_src_as_uint(instr->src[1]);
      const unsigned location = nir_intrinsic_base(instr) +
         SET_FIELD(store_offset, BRW_NIR_FRAG_OUTPUT_LOCATION);
      const brw_reg new_dest =
         offset(retype(alloc_frag_output(ntb, location), src.type),
                bld, nir_intrinsic_component(instr));

      brw_combine_with_vec(bld, new_dest, src, instr->num_components);
      break;
   }

   case nir_intrinsic_load_output: {
      const unsigned l = GET_FIELD(nir_intrinsic_base(instr),
                                   BRW_NIR_FRAG_OUTPUT_LOCATION);
      assert(l >= FRAG_RESULT_DATA0);
      const unsigned load_offset = nir_src_as_uint(instr->src[0]);
      const unsigned target = l - FRAG_RESULT_DATA0 + load_offset;
      const brw_reg tmp = bld.vgrf(dest.type, 4);

      if (reinterpret_cast<const brw_wm_prog_key *>(s.key)->coherent_fb_fetch)
         emit_coherent_fb_read(bld, tmp, target);
      else
         emit_non_coherent_fb_read(ntb, bld, tmp, target);

      brw_combine_with_vec(bld, dest,
                           offset(tmp, bld, nir_intrinsic_component(instr)),
                           instr->num_components);
      break;
   }

   case nir_intrinsic_demote:
   case nir_intrinsic_terminate:
   case nir_intrinsic_demote_if:
   case nir_intrinsic_terminate_if: {
      /* We track our discarded pixels in f0.1/f1.0.  By predicating on it, we
       * can update just the flag bits that aren't yet discarded.  If there's
       * no condition, we emit a CMP of g0 != g0, so all currently executing
       * channels will get turned off.
       */
      fs_inst *cmp = NULL;
      if (instr->intrinsic == nir_intrinsic_demote_if ||
          instr->intrinsic == nir_intrinsic_terminate_if) {
         nir_alu_instr *alu = nir_src_as_alu_instr(instr->src[0]);

         if (alu != NULL &&
             alu->op != nir_op_bcsel) {
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
            fs_nir_emit_alu(ntb, alu, false);

            cmp = (fs_inst *) s.instructions.get_tail();
            if (cmp->conditional_mod == BRW_CONDITIONAL_NONE) {
               if (cmp->can_do_cmod())
                  cmp->conditional_mod = BRW_CONDITIONAL_Z;
               else
                  cmp = NULL;
            } else {
               /* The old sequence that would have been generated is,
                * basically, bool_result == false.  This is equivalent to
                * !bool_result, so negate the old modifier.
                *
                * Unfortunately, we can't do this to most float comparisons
                * because of NaN, so we'll have to fallback to the old-style
                * compare.
                *
                * For example, this code (after negation):
                *    (+f1.0) cmp.ge.f1.0(8) null<1>F g30<8,8,1>F     0x0F
                * will provide different results from this:
                *    cmp.l.f0.0(8)   g31<1>F         g30<1,1,0>F     0x0F
                *    (+f1.0) cmp.z.f1.0(8) null<1>D  g31<8,8,1>D     0D
                * because both (NaN >= 0) == false and (NaN < 0) == false.
                *
                * It will still work for == and != though, because
                * (NaN == x) == false and (NaN != x) == true.
                */
               if (brw_type_is_float(cmp->src[0].type) &&
                   cmp->conditional_mod != BRW_CONDITIONAL_EQ &&
                   cmp->conditional_mod != BRW_CONDITIONAL_NEQ) {
                  cmp = NULL;
               } else {
                  cmp->conditional_mod = brw_negate_cmod(cmp->conditional_mod);
               }
            }
         }

         if (cmp == NULL) {
            cmp = bld.CMP(bld.null_reg_f(), get_nir_src(ntb, instr->src[0]),
                          brw_imm_d(0), BRW_CONDITIONAL_Z);
         }
      } else {
         brw_reg some_reg = brw_reg(retype(brw_vec8_grf(0, 0), BRW_TYPE_UW));
         cmp = bld.CMP(bld.null_reg_f(), some_reg, some_reg, BRW_CONDITIONAL_NZ);
      }

      cmp->predicate = BRW_PREDICATE_NORMAL;
      cmp->flag_subreg = sample_mask_flag_subreg(s);

      fs_inst *jump = bld.emit(BRW_OPCODE_HALT);
      jump->flag_subreg = sample_mask_flag_subreg(s);
      jump->predicate_inverse = true;

      if (instr->intrinsic == nir_intrinsic_terminate ||
          instr->intrinsic == nir_intrinsic_terminate_if) {
         jump->predicate = BRW_PREDICATE_NORMAL;
      } else {
         /* Only jump when the whole quad is demoted.  For historical
          * reasons this is also used for discard.
          */
         jump->predicate = (devinfo->ver >= 20 ? XE2_PREDICATE_ANY :
                            BRW_PREDICATE_ALIGN1_ANY4H);
      }
      break;
   }

   case nir_intrinsic_load_input:
   case nir_intrinsic_load_per_primitive_input: {
      /* In Fragment Shaders load_input is used either for flat inputs or
       * per-primitive inputs.
       */
      assert(instr->def.bit_size == 32);
      unsigned base = nir_intrinsic_base(instr);
      unsigned comp = nir_intrinsic_component(instr);
      unsigned num_components = instr->num_components;

      /* TODO(mesh): Multiview. Verify and handle these special cases for Mesh. */

      if (base == VARYING_SLOT_LAYER) {
         dest.type = BRW_TYPE_UD;
         bld.MOV(dest, fetch_render_target_array_index(bld));
         break;
      } else if (base == VARYING_SLOT_VIEWPORT) {
         dest.type = BRW_TYPE_UD;
         bld.MOV(dest, fetch_viewport_index(bld));
         break;
      }

      if (BITFIELD64_BIT(base) & s.nir->info.per_primitive_inputs) {
         assert(base != VARYING_SLOT_PRIMITIVE_INDICES);
         for (unsigned int i = 0; i < num_components; i++) {
            bld.MOV(offset(dest, bld, i),
                    retype(brw_per_primitive_reg(bld, base, comp + i), dest.type));
         }
      } else {
         /* Gfx20+ packs the plane parameters of a single logical
          * input in a vec3 format instead of the previously used vec4
          * format.
          */
         const unsigned k = devinfo->ver >= 20 ? 0 : 3;
         for (unsigned int i = 0; i < num_components; i++) {
            bld.MOV(offset(dest, bld, i),
                    retype(brw_interp_reg(bld, base, comp + i, k), dest.type));
         }
      }
      break;
   }

   case nir_intrinsic_load_fs_input_interp_deltas: {
      assert(s.stage == MESA_SHADER_FRAGMENT);
      assert(nir_src_as_uint(instr->src[0]) == 0);
      const unsigned base = nir_intrinsic_base(instr);
      const unsigned comp = nir_intrinsic_component(instr);
      dest.type = BRW_TYPE_F;

      /* Gfx20+ packs the plane parameters of a single logical
       * input in a vec3 format instead of the previously used vec4
       * format.
       */
      if (devinfo->ver >= 20) {
         bld.MOV(offset(dest, bld, 0), brw_interp_reg(bld, base, comp, 0));
         bld.MOV(offset(dest, bld, 1), brw_interp_reg(bld, base, comp, 2));
         bld.MOV(offset(dest, bld, 2), brw_interp_reg(bld, base, comp, 1));
      } else {
         bld.MOV(offset(dest, bld, 0), brw_interp_reg(bld, base, comp, 3));
         bld.MOV(offset(dest, bld, 1), brw_interp_reg(bld, base, comp, 1));
         bld.MOV(offset(dest, bld, 2), brw_interp_reg(bld, base, comp, 0));
      }

      break;
   }

   case nir_intrinsic_load_barycentric_pixel:
   case nir_intrinsic_load_barycentric_centroid:
   case nir_intrinsic_load_barycentric_sample: {
      /* Use the delta_xy values computed from the payload */
      enum brw_barycentric_mode bary = brw_barycentric_mode(
         reinterpret_cast<const brw_wm_prog_key *>(s.key), instr);
      const brw_reg srcs[] = { offset(s.delta_xy[bary], bld, 0),
                              offset(s.delta_xy[bary], bld, 1) };
      bld.LOAD_PAYLOAD(dest, srcs, ARRAY_SIZE(srcs), 0);
      break;
   }

   case nir_intrinsic_load_barycentric_at_sample: {
      const glsl_interp_mode interpolation =
         (enum glsl_interp_mode) nir_intrinsic_interp_mode(instr);

      if (devinfo->ver >= 20) {
         emit_pixel_interpolater_alu_at_sample(
            bld, dest, retype(get_nir_src(ntb, instr->src[0]),
                              BRW_TYPE_UD),
            interpolation);

      } else {
         brw_reg msg_data;
         if (nir_src_is_const(instr->src[0])) {
            msg_data = brw_imm_ud(nir_src_as_uint(instr->src[0]) << 4);
         } else {
            const brw_reg sample_src = retype(get_nir_src(ntb, instr->src[0]),
                                             BRW_TYPE_UD);
            const brw_reg sample_id = bld.emit_uniformize(sample_src);
            msg_data = component(bld.group(8, 0).vgrf(BRW_TYPE_UD), 0);
            bld.exec_all().group(1, 0).SHL(msg_data, sample_id, brw_imm_ud(4u));
         }

         brw_reg flag_reg;
         struct brw_wm_prog_key *wm_prog_key = (struct brw_wm_prog_key *) s.key;
         if (wm_prog_key->multisample_fbo == BRW_SOMETIMES) {
            struct brw_wm_prog_data *wm_prog_data = brw_wm_prog_data(s.prog_data);

            check_dynamic_msaa_flag(bld.exec_all().group(8, 0),
                                    wm_prog_data,
                                    INTEL_MSAA_FLAG_MULTISAMPLE_FBO);
            flag_reg = brw_flag_reg(0, 0);
         }

         emit_pixel_interpolater_send(bld,
                                      FS_OPCODE_INTERPOLATE_AT_SAMPLE,
                                      dest,
                                      brw_reg(), /* src */
                                      msg_data,
                                      flag_reg,
                                      interpolation);
      }
      break;
   }

   case nir_intrinsic_load_barycentric_at_offset: {
      const glsl_interp_mode interpolation =
         (enum glsl_interp_mode) nir_intrinsic_interp_mode(instr);

      if (devinfo->ver >= 20) {
         emit_pixel_interpolater_alu_at_offset(
            bld, dest,
            retype(get_nir_src(ntb, instr->src[0]), BRW_TYPE_F),
            interpolation);

      } else if (nir_const_value *const_offset = nir_src_as_const_value(instr->src[0])) {
         assert(nir_src_bit_size(instr->src[0]) == 32);
         unsigned off_x = const_offset[0].u32 & 0xf;
         unsigned off_y = const_offset[1].u32 & 0xf;

         emit_pixel_interpolater_send(bld,
                                      FS_OPCODE_INTERPOLATE_AT_SHARED_OFFSET,
                                      dest,
                                      brw_reg(), /* src */
                                      brw_imm_ud(off_x | (off_y << 4)),
                                      brw_reg(), /* flag_reg */
                                      interpolation);
      } else {
         brw_reg src = retype(get_nir_src(ntb, instr->src[0]), BRW_TYPE_D);
         const enum opcode opcode = FS_OPCODE_INTERPOLATE_AT_PER_SLOT_OFFSET;
         emit_pixel_interpolater_send(bld,
                                      opcode,
                                      dest,
                                      src,
                                      brw_imm_ud(0u),
                                      brw_reg(), /* flag_reg */
                                      interpolation);
      }
      break;
   }

   case nir_intrinsic_load_frag_coord: {
      brw_reg comps[4] = { s.pixel_x, s.pixel_y, s.pixel_z, s.wpos_w };
      bld.VEC(dest, comps, 4);
      break;
   }

   case nir_intrinsic_load_interpolated_input: {
      assert(instr->src[0].ssa &&
             instr->src[0].ssa->parent_instr->type == nir_instr_type_intrinsic);
      nir_intrinsic_instr *bary_intrinsic =
         nir_instr_as_intrinsic(instr->src[0].ssa->parent_instr);
      nir_intrinsic_op bary_intrin = bary_intrinsic->intrinsic;
      brw_reg dst_xy;

      if (bary_intrin == nir_intrinsic_load_barycentric_at_offset ||
          bary_intrin == nir_intrinsic_load_barycentric_at_sample) {
         /* Use the result of the PI message. */
         dst_xy = retype(get_nir_src(ntb, instr->src[0]), BRW_TYPE_F);
      } else {
         /* Use the delta_xy values computed from the payload */
         enum brw_barycentric_mode bary = brw_barycentric_mode(
            reinterpret_cast<const brw_wm_prog_key *>(s.key), bary_intrinsic);
         dst_xy = s.delta_xy[bary];
      }

      for (unsigned int i = 0; i < instr->num_components; i++) {
         brw_reg interp =
            brw_interp_reg(bld, nir_intrinsic_base(instr),
                           nir_intrinsic_component(instr) + i, 0);
         interp.type = BRW_TYPE_F;
         dest.type = BRW_TYPE_F;

         bld.PLN(offset(dest, bld, i), interp, dst_xy);
      }
      break;
   }

   default:
      fs_nir_emit_intrinsic(ntb, bld, instr);
      break;
   }
}

static unsigned
brw_workgroup_size(fs_visitor &s)
{
   assert(gl_shader_stage_uses_workgroup(s.stage));
   assert(!s.nir->info.workgroup_size_variable);
   const struct brw_cs_prog_data *cs = brw_cs_prog_data(s.prog_data);
   return cs->local_size[0] * cs->local_size[1] * cs->local_size[2];
}

static void
fs_nir_emit_cs_intrinsic(nir_to_brw_state &ntb,
                         nir_intrinsic_instr *instr)
{
   const intel_device_info *devinfo = ntb.devinfo;
   const fs_builder &bld = ntb.bld;
   fs_visitor &s = ntb.s;

   assert(gl_shader_stage_uses_workgroup(s.stage));
   struct brw_cs_prog_data *cs_prog_data = brw_cs_prog_data(s.prog_data);

   brw_reg dest;
   if (nir_intrinsic_infos[instr->intrinsic].has_dest)
      dest = get_nir_def(ntb, instr->def);

   switch (instr->intrinsic) {
   case nir_intrinsic_barrier:
      if (nir_intrinsic_memory_scope(instr) != SCOPE_NONE)
         fs_nir_emit_intrinsic(ntb, bld, instr);
      if (nir_intrinsic_execution_scope(instr) == SCOPE_WORKGROUP) {
         /* The whole workgroup fits in a single HW thread, so all the
          * invocations are already executed lock-step.  Instead of an actual
          * barrier just emit a scheduling fence, that will generate no code.
          */
         if (!s.nir->info.workgroup_size_variable &&
             brw_workgroup_size(s) <= s.dispatch_width) {
            bld.exec_all().group(1, 0).emit(FS_OPCODE_SCHEDULING_FENCE);
            break;
         }

         emit_barrier(ntb);
         cs_prog_data->uses_barrier = true;
      }
      break;

   case nir_intrinsic_load_inline_data_intel: {
      const cs_thread_payload &payload = s.cs_payload();
      unsigned inline_stride = brw_type_size_bytes(dest.type);
      for (unsigned c = 0; c < instr->def.num_components; c++)
         bld.MOV(offset(dest, bld, c),
                 retype(
                    byte_offset(payload.inline_parameter,
                                nir_intrinsic_base(instr) +
                                c * inline_stride),
                    dest.type));
      break;
   }

   case nir_intrinsic_load_subgroup_id:
      s.cs_payload().load_subgroup_id(bld, dest);
      break;

   case nir_intrinsic_load_local_invocation_id:
      /* This is only used for hardware generated local IDs. */
      assert(cs_prog_data->generate_local_id);

      dest.type = BRW_TYPE_UD;

      for (unsigned i = 0; i < 3; i++)
         bld.MOV(offset(dest, bld, i), s.cs_payload().local_invocation_id[i]);
      break;

   case nir_intrinsic_load_workgroup_id: {
      brw_reg val = ntb.system_values[SYSTEM_VALUE_WORKGROUP_ID];
      assert(val.file != BAD_FILE);
      dest.type = val.type;
      for (unsigned i = 0; i < 3; i++)
         bld.MOV(offset(dest, bld, i), offset(val, bld, i));
      break;
   }

   case nir_intrinsic_load_num_workgroups: {
      assert(instr->def.bit_size == 32);

      cs_prog_data->uses_num_work_groups = true;

      brw_reg srcs[MEMORY_LOGICAL_NUM_SRCS];
      srcs[MEMORY_LOGICAL_OPCODE] = brw_imm_ud(LSC_OP_LOAD);
      srcs[MEMORY_LOGICAL_MODE] = brw_imm_ud(MEMORY_MODE_UNTYPED);
      srcs[MEMORY_LOGICAL_BINDING_TYPE] = brw_imm_ud(LSC_ADDR_SURFTYPE_BTI);
      srcs[MEMORY_LOGICAL_BINDING] = brw_imm_ud(0);
      srcs[MEMORY_LOGICAL_ADDRESS] = brw_imm_ud(0);
      srcs[MEMORY_LOGICAL_COORD_COMPONENTS] = brw_imm_ud(1);
      srcs[MEMORY_LOGICAL_ALIGNMENT] = brw_imm_ud(4);
      srcs[MEMORY_LOGICAL_DATA_SIZE] = brw_imm_ud(LSC_DATA_SIZE_D32);
      srcs[MEMORY_LOGICAL_COMPONENTS] = brw_imm_ud(3);
      srcs[MEMORY_LOGICAL_FLAGS] = brw_imm_ud(0);

      fs_inst *inst =
         bld.emit(SHADER_OPCODE_MEMORY_LOAD_LOGICAL,
                  dest, srcs, MEMORY_LOGICAL_NUM_SRCS);
      inst->size_written = 3 * s.dispatch_width * 4;
      break;
   }

   case nir_intrinsic_load_workgroup_size: {
      /* Should have been lowered by brw_nir_lower_cs_intrinsics() or
       * crocus/iris_setup_uniforms() for the variable group size case.
       */
      unreachable("Should have been lowered");
      break;
   }

   case nir_intrinsic_dpas_intel: {
      const unsigned sdepth = nir_intrinsic_systolic_depth(instr);
      const unsigned rcount = nir_intrinsic_repeat_count(instr);

      const brw_reg_type dest_type =
         brw_type_for_nir_type(devinfo, nir_intrinsic_dest_type(instr));
      const brw_reg_type src_type =
         brw_type_for_nir_type(devinfo, nir_intrinsic_src_type(instr));

      dest = retype(dest, dest_type);
      brw_reg src0 = retype(get_nir_src(ntb, instr->src[0]), dest_type);

      fs_builder bld16 = bld.exec_all().group(16, 0);
      fs_builder bldn = devinfo->ver >= 20 ? bld16 : bld.exec_all().group(8, 0);

      bldn.DPAS(dest,
                src0,
                retype(get_nir_src(ntb, instr->src[2]), src_type),
                retype(get_nir_src(ntb, instr->src[1]), src_type),
                sdepth,
                rcount)
         ->saturate = nir_intrinsic_saturate(instr);

      cs_prog_data->uses_systolic = true;
      break;
   }

   default:
      fs_nir_emit_intrinsic(ntb, bld, instr);
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
   brw_reg tmp = ubld.vgrf(BRW_TYPE_UD);
   fs_inst *send = ubld.emit(SHADER_OPCODE_SEND, tmp,
                             brw_imm_ud(0) /* desc */,
                             brw_imm_ud(0) /* ex_desc */,
                             brw_vec8_grf(0, 0) /* payload */);
   send->sfid = GFX12_SFID_UGM;
   send->desc = lsc_fence_msg_desc(devinfo, scope, flush_type, true);
   send->mlen = reg_unit(devinfo); /* g0 header */
   send->ex_mlen = 0;
   /* Temp write for scheduling */
   send->size_written = REG_SIZE * reg_unit(devinfo);
   send->send_has_side_effects = true;

   ubld.emit(FS_OPCODE_SCHEDULING_FENCE, ubld.null_reg_ud(), tmp);
}


static void
fs_nir_emit_bs_intrinsic(nir_to_brw_state &ntb,
                         nir_intrinsic_instr *instr)
{
   const fs_builder &bld = ntb.bld;
   fs_visitor &s = ntb.s;

   assert(brw_shader_stage_is_bindless(s.stage));
   const bs_thread_payload &payload = s.bs_payload();

   brw_reg dest;
   if (nir_intrinsic_infos[instr->intrinsic].has_dest)
      dest = get_nir_def(ntb, instr->def);

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
      fs_nir_emit_intrinsic(ntb, bld, instr);
      break;
   }
}

static brw_reduce_op
brw_reduce_op_for_nir_reduction_op(nir_op op)
{
   switch (op) {
   case nir_op_iadd: return BRW_REDUCE_OP_ADD;
   case nir_op_fadd: return BRW_REDUCE_OP_ADD;
   case nir_op_imul: return BRW_REDUCE_OP_MUL;
   case nir_op_fmul: return BRW_REDUCE_OP_MUL;
   case nir_op_imin: return BRW_REDUCE_OP_MIN;
   case nir_op_umin: return BRW_REDUCE_OP_MIN;
   case nir_op_fmin: return BRW_REDUCE_OP_MIN;
   case nir_op_imax: return BRW_REDUCE_OP_MAX;
   case nir_op_umax: return BRW_REDUCE_OP_MAX;
   case nir_op_fmax: return BRW_REDUCE_OP_MAX;
   case nir_op_iand: return BRW_REDUCE_OP_AND;
   case nir_op_ior:  return BRW_REDUCE_OP_OR;
   case nir_op_ixor: return BRW_REDUCE_OP_XOR;
   default:
      unreachable("Invalid reduction operation");
   }
}

struct rebuild_resource {
   unsigned idx;
   std::vector<nir_def *> array;
};

static bool
skip_rebuild_instr(nir_instr *instr)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   switch (intrin->intrinsic) {
   case nir_intrinsic_load_ubo_uniform_block_intel:
   case nir_intrinsic_load_ssbo_uniform_block_intel:
   case nir_intrinsic_load_global_constant_uniform_block_intel:
      /* Those intrinsic are generated using NoMask so we can trust their
       * destination registers are fully populated. No need to rematerialize
       * further.
       */
      return true;

   default:
      return false;
   }
}

static bool
add_rebuild_src(nir_src *src, void *state)
{
   struct rebuild_resource *res = (struct rebuild_resource *) state;

   for (nir_def *def : res->array) {
      if (def == src->ssa)
         return true;
   }

   if (!skip_rebuild_instr(src->ssa->parent_instr))
      nir_foreach_src(src->ssa->parent_instr, add_rebuild_src, state);
   res->array.push_back(src->ssa);
   return true;
}

static brw_reg
try_rebuild_source(nir_to_brw_state &ntb, const brw::fs_builder &bld,
                   nir_def *resource_def, bool a64 = false)
{
   /* Create a build at the location of the resource_intel intrinsic */
   fs_builder ubld = bld.exec_all().group(8 * reg_unit(ntb.devinfo), 0);
   const unsigned grf_size = REG_SIZE * reg_unit(ntb.devinfo);

   struct rebuild_resource resources = {};
   resources.idx = 0;

   if (!nir_foreach_src(resource_def->parent_instr,
                        add_rebuild_src, &resources))
      return brw_reg();
   resources.array.push_back(resource_def);

#if 0
   fprintf(stderr, "Trying remat :\n");
   for (unsigned i = 0; i < resources.array.size(); i++) {
      fprintf(stderr, "   ");
      nir_print_instr(resources.array[i]->parent_instr, stderr);
      fprintf(stderr, "\n");
   }
#endif

   for (unsigned i = 0; i < resources.array.size(); i++) {
      nir_def *def = resources.array[i];

      nir_instr *instr = def->parent_instr;
      switch (instr->type) {
      case nir_instr_type_load_const: {
         nir_load_const_instr *load_const =
            nir_instr_as_load_const(instr);
         ubld.MOV(brw_imm_d(load_const->value[0].i32),
                  &ntb.resource_insts[def->index]);
         break;
      }

      case nir_instr_type_alu: {
         nir_alu_instr *alu = nir_instr_as_alu(instr);

         /* Not supported ALU source count */
         if (nir_op_infos[alu->op].num_inputs > 3)
            break;

         brw_reg srcs[3];
         for (unsigned s = 0; s < nir_op_infos[alu->op].num_inputs; s++) {
            srcs[s] = offset(
               ntb.resource_insts[alu->src[s].src.ssa->index]->dst,
               ubld, alu->src[s].swizzle[0]);
            assert(srcs[s].file != BAD_FILE);
         }

         switch (alu->op) {
         case nir_op_iadd:
            ubld.ADD(srcs[0].file != IMM ? srcs[0] : srcs[1],
                     srcs[0].file != IMM ? srcs[1] : srcs[0],
                     &ntb.resource_insts[def->index]);
            break;
         case nir_op_iadd3: {
            brw_reg dst = ubld.vgrf(srcs[0].type);
            ntb.resource_insts[def->index] =
               ubld.ADD3(dst,
                         srcs[1].file == IMM ? srcs[1] : srcs[0],
                         srcs[1].file == IMM ? srcs[0] : srcs[1],
                         srcs[2]);
            break;
         }
         case nir_op_ushr: {
            enum brw_reg_type utype =
               brw_type_with_size(BRW_TYPE_UD,
                                  brw_type_size_bits(srcs[0].type));
            ubld.SHR(retype(srcs[0], utype),
                     retype(srcs[1], utype),
                     &ntb.resource_insts[def->index]);
            break;
         }
         case nir_op_iand:
            ubld.AND(srcs[0], srcs[1], &ntb.resource_insts[def->index]);
            break;
         case nir_op_ishl:
            ubld.SHL(srcs[0], srcs[1], &ntb.resource_insts[def->index]);
            break;
         case nir_op_mov:
            break;
         case nir_op_ult32: {
            if (brw_type_size_bits(srcs[0].type) != 32)
               break;
            enum brw_reg_type utype =
               brw_type_with_size(BRW_TYPE_UD,
                                  brw_type_size_bits(srcs[0].type));
            brw_reg dst = ubld.vgrf(utype);
            ntb.resource_insts[def->index] =
               ubld.CMP(dst,
                        retype(srcs[0], utype),
                        retype(srcs[1], utype),
                        brw_cmod_for_nir_comparison(alu->op));
            break;
         }
         case nir_op_b2i32:
            ubld.MOV(negate(retype(srcs[0], BRW_TYPE_D)),
                     &ntb.resource_insts[def->index]);
            break;
         case nir_op_unpack_64_2x32_split_x:
            ubld.MOV(subscript(srcs[0], BRW_TYPE_D, 0),
                     &ntb.resource_insts[def->index]);
            break;
         case nir_op_unpack_64_2x32_split_y:
            ubld.MOV(subscript(srcs[0], BRW_TYPE_D, 1),
                     &ntb.resource_insts[def->index]);
            break;
         case nir_op_pack_64_2x32_split: {
            brw_reg dst = ubld.vgrf(BRW_TYPE_Q);
            ntb.resource_insts[def->index] =
               ubld.emit(FS_OPCODE_PACK, dst, srcs[0], srcs[1]);
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
            ntb.resource_insts[def->index] =
               ntb.resource_insts[intrin->src[1].ssa->index];
            break;

         case nir_intrinsic_load_uniform: {
            if (!nir_src_is_const(intrin->src[0]))
               break;

            unsigned base_offset = nir_intrinsic_base(intrin);
            unsigned load_offset = nir_src_as_uint(intrin->src[0]);

            enum brw_reg_type type =
               brw_type_with_size(BRW_TYPE_D, intrin->def.bit_size);
            brw_reg dst_data = ubld.vgrf(type, intrin->def.num_components);

            for (unsigned c = 0; c < intrin->def.num_components; c++) {
               brw_reg src = brw_uniform_reg(base_offset / 4, type);
               src.offset = load_offset + base_offset % 4 + c * intrin->def.bit_size / 8;
               fs_inst *inst = ubld.MOV(byte_offset(dst_data, c * grf_size), src);
               if (c == 0)
                  ntb.resource_insts[def->index] = inst;
            }
            break;
         }

         case nir_intrinsic_load_inline_data_intel: {
            assert(brw_shader_stage_has_inline_data(ntb.devinfo, ntb.s.stage));
            const cs_thread_payload &payload = ntb.s.cs_payload();
            enum brw_reg_type type =
               brw_type_with_size(BRW_TYPE_D, intrin->def.bit_size);
            brw_reg dst_data = ubld.vgrf(type, intrin->def.num_components);
            unsigned inline_stride = brw_type_size_bytes(type);

            for (unsigned c = 0; c < intrin->def.num_components; c++) {
               fs_inst *inst = ubld.MOV(byte_offset(dst_data, c * grf_size),
                                        retype(
                                           byte_offset(payload.inline_parameter,
                                                       nir_intrinsic_base(intrin) +
                                                       c * inline_stride),
                                           type));
               if (c == 0)
                  ntb.resource_insts[def->index] = inst;
            }
            break;
         }

         case nir_intrinsic_load_btd_local_arg_addr_intel: {
            assert(brw_shader_stage_is_bindless(ntb.s.stage));
            const bs_thread_payload &payload = ntb.s.bs_payload();
            ubld.MOV(retype(payload.local_arg_ptr, BRW_TYPE_Q),
                     &ntb.resource_insts[def->index]);
            break;
         }

         case nir_intrinsic_load_btd_global_arg_addr_intel: {
            assert(brw_shader_stage_is_bindless(ntb.s.stage));
            const bs_thread_payload &payload = ntb.s.bs_payload();
            ubld.MOV(retype(payload.global_arg_ptr, BRW_TYPE_Q),
                     &ntb.resource_insts[def->index]);
            break;
         }

         case nir_intrinsic_load_reloc_const_intel: {
            uint32_t id = nir_intrinsic_param_idx(intrin);
            brw_reg dst = ubld.vgrf(BRW_TYPE_D);
            ntb.resource_insts[def->index] =
               ubld.emit(SHADER_OPCODE_MOV_RELOC_IMM, dst,
                         brw_imm_ud(id), brw_imm_ud(0));
            break;
         }

         case nir_intrinsic_load_ubo_uniform_block_intel:
         case nir_intrinsic_load_ssbo_uniform_block_intel: {
            enum brw_reg_type type =
               brw_type_with_size(BRW_TYPE_D, intrin->def.bit_size);
            brw_reg src_data = retype(ntb.ssa_values[def->index], type);
            unsigned n_components = ntb.s.alloc.sizes[src_data.nr] /
                                    (bld.dispatch_width() / 8);
            brw_reg dst_data = ubld.vgrf(type, n_components);
            ntb.resource_insts[def->index] = ubld.MOV(dst_data, src_data);
            for (unsigned c = 1; c < n_components; c++) {
               ubld.MOV(offset(dst_data, ubld, c),
                        offset(src_data, bld, c));
            }
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

      if (ntb.resource_insts[def->index] == NULL) {
#if 0
         if (a64) {
         fprintf(stderr, "Tried remat :\n");
         for (unsigned i = 0; i < resources.array.size(); i++) {
            fprintf(stderr, "   ");
            nir_print_instr(resources.array[i]->parent_instr, stderr);
            fprintf(stderr, "\n");
         }
         fprintf(stderr, "failed at! : ");
         nir_print_instr(instr, stderr);
         fprintf(stderr, "\n");
         }
#endif
         return brw_reg();
      }
   }

   assert(ntb.resource_insts[resource_def->index] != NULL);
   return component(ntb.resource_insts[resource_def->index]->dst, 0);
}

static brw_reg
get_nir_image_intrinsic_image(nir_to_brw_state &ntb, const brw::fs_builder &bld,
                              nir_intrinsic_instr *instr)
{
   if (is_resource_src(instr->src[0])) {
      brw_reg surf_index = get_resource_nir_src(ntb, instr->src[0]);
      if (surf_index.file != BAD_FILE)
         return surf_index;
   }

   brw_reg image = retype(get_nir_src_imm(ntb, instr->src[0]), BRW_TYPE_UD);
   brw_reg surf_index = image;

   return bld.emit_uniformize(surf_index);
}

static brw_reg
get_nir_buffer_intrinsic_index(nir_to_brw_state &ntb, const brw::fs_builder &bld,
                               nir_intrinsic_instr *instr, bool *no_mask_handle = NULL)
{
   /* SSBO stores are weird in that their index is in src[1] */
   const bool is_store =
      instr->intrinsic == nir_intrinsic_store_ssbo ||
      instr->intrinsic == nir_intrinsic_store_ssbo_block_intel;
   nir_src src = is_store ? instr->src[1] : instr->src[0];

   if (no_mask_handle)
      *no_mask_handle = false;

   if (nir_src_is_const(src)) {
      if (no_mask_handle)
         *no_mask_handle = true;
      return brw_imm_ud(nir_src_as_uint(src));
   } else if (is_resource_src(src)) {
      brw_reg surf_index = get_resource_nir_src(ntb, src);
      if (surf_index.file != BAD_FILE) {
         if (no_mask_handle)
            *no_mask_handle = true;
         return surf_index;
      }
   }
   return bld.emit_uniformize(get_nir_src(ntb, src));
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
static brw_reg
swizzle_nir_scratch_addr(nir_to_brw_state &ntb,
                         const brw::fs_builder &bld,
                         const nir_src &nir_addr_src,
                         bool in_dwords)
{
   fs_visitor &s = ntb.s;

   const brw_reg chan_index = bld.LOAD_SUBGROUP_INVOCATION();
   const unsigned chan_index_bits = ffs(s.dispatch_width) - 1;

   if (nir_src_is_const(nir_addr_src)) {
      unsigned nir_addr = nir_src_as_uint(nir_addr_src);
      if (in_dwords) {
         /* In this case, we know the address is aligned to a DWORD and we want
          * the final address in DWORDs.
          */
         return bld.OR(chan_index,
                       brw_imm_ud(nir_addr << (chan_index_bits - 2)));
      } else {
         /* This case is substantially more annoying because we have to pay
          * attention to those pesky two bottom bits.
          */
         unsigned addr_hi = (nir_addr & ~0x3u) << chan_index_bits;
         unsigned addr_lo = (nir_addr &  0x3u);

         return bld.OR(bld.SHL(chan_index, brw_imm_ud(2)),
                       brw_imm_ud(addr_lo | addr_hi));
      }
   }

   const brw_reg nir_addr =
      retype(get_nir_src(ntb, nir_addr_src), BRW_TYPE_UD);

   if (in_dwords) {
      /* In this case, we know the address is aligned to a DWORD and we want
       * the final address in DWORDs.
       */
      return bld.OR(bld.SHL(nir_addr, brw_imm_ud(chan_index_bits - 2)),
                    chan_index);
   } else {
      /* This case substantially more annoying because we have to pay
       * attention to those pesky two bottom bits.
       */
      brw_reg chan_addr = bld.SHL(chan_index, brw_imm_ud(2));
      brw_reg addr_bits =
         bld.OR(bld.AND(nir_addr, brw_imm_ud(0x3u)),
                bld.SHL(bld.AND(nir_addr, brw_imm_ud(~0x3u)),
                        brw_imm_ud(chan_index_bits)));
      return bld.OR(addr_bits, chan_addr);
   }
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

static brw_reg
increment_a64_address(const fs_builder &_bld, brw_reg address, uint32_t v, bool use_no_mask)
{
   const fs_builder bld = use_no_mask ? _bld.exec_all().group(8, 0) : _bld;

   if (bld.shader->devinfo->has_64bit_int) {
      struct brw_reg imm = brw_imm_reg(address.type);
      imm.u64 = v;
      return bld.ADD(address, imm);
   } else {
      brw_reg dst = bld.vgrf(BRW_TYPE_UQ);
      brw_reg dst_low = subscript(dst, BRW_TYPE_UD, 0);
      brw_reg dst_high = subscript(dst, BRW_TYPE_UD, 1);
      brw_reg src_low = subscript(address, BRW_TYPE_UD, 0);
      brw_reg src_high = subscript(address, BRW_TYPE_UD, 1);

      /* Add low and if that overflows, add carry to high. */
      bld.ADD(dst_low, src_low, brw_imm_ud(v))->conditional_mod = BRW_CONDITIONAL_O;
      bld.ADD(dst_high, src_high, brw_imm_ud(0x1))->predicate = BRW_PREDICATE_NORMAL;
      return dst_low;
   }
}

static brw_reg
emit_fence(const fs_builder &bld, enum opcode opcode,
           uint8_t sfid, uint32_t desc,
           bool commit_enable, uint8_t bti)
{
   assert(opcode == SHADER_OPCODE_INTERLOCK ||
          opcode == SHADER_OPCODE_MEMORY_FENCE);

   brw_reg dst = bld.vgrf(BRW_TYPE_UD);
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

/**
 * Create a MOV to read the timestamp register.
 */
static brw_reg
get_timestamp(const fs_builder &bld)
{
   fs_visitor &s = *bld.shader;

   brw_reg ts = brw_reg(retype(brw_vec4_reg(ARF,
                                          BRW_ARF_TIMESTAMP, 0), BRW_TYPE_UD));

   brw_reg dst = brw_vgrf(s.alloc.allocate(1), BRW_TYPE_UD);

   /* We want to read the 3 fields we care about even if it's not enabled in
    * the dispatch.
    */
   bld.group(4, 0).exec_all().MOV(dst, ts);

   return dst;
}

static unsigned
component_from_intrinsic(nir_intrinsic_instr *instr)
{
   if (nir_intrinsic_has_component(instr))
      return nir_intrinsic_component(instr);
   else
      return 0;
}

static void
adjust_handle_and_offset(const fs_builder &bld,
                         brw_reg &urb_handle,
                         unsigned &urb_global_offset)
{
   /* Make sure that URB global offset is below 2048 (2^11), because
    * that's the maximum possible value encoded in Message Descriptor.
    */
   unsigned adjustment = (urb_global_offset >> 11) << 11;

   if (adjustment) {
      fs_builder ubld8 = bld.group(8, 0).exec_all();
      /* Allocate new register to not overwrite the shared URB handle. */
      urb_handle = ubld8.ADD(urb_handle, brw_imm_ud(adjustment));
      urb_global_offset -= adjustment;
   }
}

static void
emit_urb_direct_vec4_write(const fs_builder &bld,
                           unsigned urb_global_offset,
                           const brw_reg &src,
                           brw_reg urb_handle,
                           unsigned dst_comp_offset,
                           unsigned comps,
                           unsigned mask)
{
   for (unsigned q = 0; q < bld.dispatch_width() / 8; q++) {
      fs_builder bld8 = bld.group(8, q);

      brw_reg payload_srcs[8];
      unsigned length = 0;

      for (unsigned i = 0; i < dst_comp_offset; i++)
         payload_srcs[length++] = reg_undef;

      for (unsigned c = 0; c < comps; c++)
         payload_srcs[length++] = quarter(offset(src, bld, c), q);

      brw_reg srcs[URB_LOGICAL_NUM_SRCS];
      srcs[URB_LOGICAL_SRC_HANDLE] = urb_handle;
      srcs[URB_LOGICAL_SRC_CHANNEL_MASK] = brw_imm_ud(mask << 16);
      srcs[URB_LOGICAL_SRC_DATA] = brw_vgrf(bld.shader->alloc.allocate(length),
                                            BRW_TYPE_F);
      srcs[URB_LOGICAL_SRC_COMPONENTS] = brw_imm_ud(length);
      bld8.LOAD_PAYLOAD(srcs[URB_LOGICAL_SRC_DATA], payload_srcs, length, 0);

      fs_inst *inst = bld8.emit(SHADER_OPCODE_URB_WRITE_LOGICAL,
                                reg_undef, srcs, ARRAY_SIZE(srcs));
      inst->offset = urb_global_offset;
      assert(inst->offset < 2048);
   }
}

static void
emit_urb_direct_writes(const fs_builder &bld, nir_intrinsic_instr *instr,
                       const brw_reg &src, brw_reg urb_handle)
{
   assert(nir_src_bit_size(instr->src[0]) == 32);

   nir_src *offset_nir_src = nir_get_io_offset_src(instr);
   assert(nir_src_is_const(*offset_nir_src));

   const unsigned comps = nir_src_num_components(instr->src[0]);
   assert(comps <= 4);

   const unsigned offset_in_dwords = nir_intrinsic_base(instr) +
                                     nir_src_as_uint(*offset_nir_src) +
                                     component_from_intrinsic(instr);

   /* URB writes are vec4 aligned but the intrinsic offsets are in dwords.
    * We can write up to 8 dwords, so single vec4 write is enough.
    */
   const unsigned comp_shift = offset_in_dwords % 4;
   const unsigned mask = nir_intrinsic_write_mask(instr) << comp_shift;

   unsigned urb_global_offset = offset_in_dwords / 4;
   adjust_handle_and_offset(bld, urb_handle, urb_global_offset);

   emit_urb_direct_vec4_write(bld, urb_global_offset, src, urb_handle,
                              comp_shift, comps, mask);
}

static void
emit_urb_direct_vec4_write_xe2(const fs_builder &bld,
                               unsigned offset_in_bytes,
                               const brw_reg &src,
                               brw_reg urb_handle,
                               unsigned comps,
                               unsigned mask)
{
   const struct intel_device_info *devinfo = bld.shader->devinfo;
   const unsigned runit = reg_unit(devinfo);
   const unsigned write_size = 8 * runit;

   if (offset_in_bytes > 0) {
      fs_builder bldall = bld.group(write_size, 0).exec_all();
      urb_handle = bldall.ADD(urb_handle, brw_imm_ud(offset_in_bytes));
   }

   for (unsigned q = 0; q < bld.dispatch_width() / write_size; q++) {
      fs_builder hbld = bld.group(write_size, q);

      assert(comps <= 4);
      brw_reg payload_srcs[4];

      for (unsigned c = 0; c < comps; c++)
         payload_srcs[c] = horiz_offset(offset(src, bld, c), write_size * q);

      brw_reg srcs[URB_LOGICAL_NUM_SRCS];
      srcs[URB_LOGICAL_SRC_HANDLE] = urb_handle;
      srcs[URB_LOGICAL_SRC_CHANNEL_MASK] = brw_imm_ud(mask << 16);
      int nr = bld.shader->alloc.allocate(comps * runit);
      srcs[URB_LOGICAL_SRC_DATA] = brw_vgrf(nr, BRW_TYPE_F);
      srcs[URB_LOGICAL_SRC_COMPONENTS] = brw_imm_ud(comps);
      hbld.LOAD_PAYLOAD(srcs[URB_LOGICAL_SRC_DATA], payload_srcs, comps, 0);

      hbld.emit(SHADER_OPCODE_URB_WRITE_LOGICAL,
                reg_undef, srcs, ARRAY_SIZE(srcs));
   }
}

static void
emit_urb_direct_writes_xe2(const fs_builder &bld, nir_intrinsic_instr *instr,
                           const brw_reg &src, brw_reg urb_handle)
{
   assert(nir_src_bit_size(instr->src[0]) == 32);

   nir_src *offset_nir_src = nir_get_io_offset_src(instr);
   assert(nir_src_is_const(*offset_nir_src));

   const unsigned comps = nir_src_num_components(instr->src[0]);
   assert(comps <= 4);

   const unsigned offset_in_dwords = nir_intrinsic_base(instr) +
                                     nir_src_as_uint(*offset_nir_src) +
                                     component_from_intrinsic(instr);

   const unsigned mask = nir_intrinsic_write_mask(instr);

   emit_urb_direct_vec4_write_xe2(bld, offset_in_dwords * 4, src,
                                    urb_handle, comps, mask);
}

static void
emit_urb_indirect_vec4_write(const fs_builder &bld,
                             const brw_reg &offset_src,
                             unsigned base,
                             const brw_reg &src,
                             brw_reg urb_handle,
                             unsigned dst_comp_offset,
                             unsigned comps,
                             unsigned mask)
{
   for (unsigned q = 0; q < bld.dispatch_width() / 8; q++) {
      fs_builder bld8 = bld.group(8, q);

      /* offset is always positive, so signedness doesn't matter */
      assert(offset_src.type == BRW_TYPE_D || offset_src.type == BRW_TYPE_UD);
      brw_reg qtr = bld8.MOV(quarter(retype(offset_src, BRW_TYPE_UD), q));
      brw_reg off = bld8.SHR(bld8.ADD(qtr, brw_imm_ud(base)), brw_imm_ud(2));

      brw_reg payload_srcs[8];
      unsigned length = 0;

      for (unsigned i = 0; i < dst_comp_offset; i++)
         payload_srcs[length++] = reg_undef;

      for (unsigned c = 0; c < comps; c++)
         payload_srcs[length++] = quarter(offset(src, bld, c), q);

      brw_reg srcs[URB_LOGICAL_NUM_SRCS];
      srcs[URB_LOGICAL_SRC_HANDLE] = urb_handle;
      srcs[URB_LOGICAL_SRC_PER_SLOT_OFFSETS] = off;
      srcs[URB_LOGICAL_SRC_CHANNEL_MASK] = brw_imm_ud(mask << 16);
      srcs[URB_LOGICAL_SRC_DATA] = brw_vgrf(bld.shader->alloc.allocate(length),
                                            BRW_TYPE_F);
      srcs[URB_LOGICAL_SRC_COMPONENTS] = brw_imm_ud(length);
      bld8.LOAD_PAYLOAD(srcs[URB_LOGICAL_SRC_DATA], payload_srcs, length, 0);

      fs_inst *inst = bld8.emit(SHADER_OPCODE_URB_WRITE_LOGICAL,
                                reg_undef, srcs, ARRAY_SIZE(srcs));
      inst->offset = 0;
   }
}

static void
emit_urb_indirect_writes_mod(const fs_builder &bld, nir_intrinsic_instr *instr,
                             const brw_reg &src, const brw_reg &offset_src,
                             brw_reg urb_handle, unsigned mod)
{
   assert(nir_src_bit_size(instr->src[0]) == 32);

   const unsigned comps = nir_src_num_components(instr->src[0]);
   assert(comps <= 4);

   const unsigned base_in_dwords = nir_intrinsic_base(instr) +
                                   component_from_intrinsic(instr);

   const unsigned comp_shift = mod;
   const unsigned mask = nir_intrinsic_write_mask(instr) << comp_shift;

   emit_urb_indirect_vec4_write(bld, offset_src, base_in_dwords, src,
                                urb_handle, comp_shift, comps, mask);
}

static void
emit_urb_indirect_writes_xe2(const fs_builder &bld, nir_intrinsic_instr *instr,
                             const brw_reg &src, const brw_reg &offset_src,
                             brw_reg urb_handle)
{
   assert(nir_src_bit_size(instr->src[0]) == 32);

   const struct intel_device_info *devinfo = bld.shader->devinfo;
   const unsigned runit = reg_unit(devinfo);
   const unsigned write_size = 8 * runit;

   const unsigned comps = nir_src_num_components(instr->src[0]);
   assert(comps <= 4);

   const unsigned base_in_dwords = nir_intrinsic_base(instr) +
                                   component_from_intrinsic(instr);

   if (base_in_dwords > 0) {
      fs_builder bldall = bld.group(write_size, 0).exec_all();
      urb_handle = bldall.ADD(urb_handle, brw_imm_ud(base_in_dwords * 4));
   }

   const unsigned mask = nir_intrinsic_write_mask(instr);

   for (unsigned q = 0; q < bld.dispatch_width() / write_size; q++) {
      fs_builder wbld = bld.group(write_size, q);

      brw_reg payload_srcs[4];

      for (unsigned c = 0; c < comps; c++)
         payload_srcs[c] = horiz_offset(offset(src, bld, c), write_size * q);

      brw_reg addr =
         wbld.ADD(wbld.SHL(retype(horiz_offset(offset_src, write_size * q),
                                  BRW_TYPE_UD),
                           brw_imm_ud(2)), urb_handle);

      brw_reg srcs[URB_LOGICAL_NUM_SRCS];
      srcs[URB_LOGICAL_SRC_HANDLE] = addr;
      srcs[URB_LOGICAL_SRC_CHANNEL_MASK] = brw_imm_ud(mask << 16);
      int nr = bld.shader->alloc.allocate(comps * runit);
      srcs[URB_LOGICAL_SRC_DATA] = brw_vgrf(nr, BRW_TYPE_F);
      srcs[URB_LOGICAL_SRC_COMPONENTS] = brw_imm_ud(comps);
      wbld.LOAD_PAYLOAD(srcs[URB_LOGICAL_SRC_DATA], payload_srcs, comps, 0);

      wbld.emit(SHADER_OPCODE_URB_WRITE_LOGICAL,
                reg_undef, srcs, ARRAY_SIZE(srcs));
   }
}

static void
emit_urb_indirect_writes(const fs_builder &bld, nir_intrinsic_instr *instr,
                         const brw_reg &src, const brw_reg &offset_src,
                         brw_reg urb_handle)
{
   assert(nir_src_bit_size(instr->src[0]) == 32);

   const unsigned comps = nir_src_num_components(instr->src[0]);
   assert(comps <= 4);

   const unsigned base_in_dwords = nir_intrinsic_base(instr) +
                                   component_from_intrinsic(instr);

   /* Use URB write message that allow different offsets per-slot.  The offset
    * is in units of vec4s (128 bits), so we use a write for each component,
    * replicating it in the sources and applying the appropriate mask based on
    * the dword offset.
    */

   for (unsigned c = 0; c < comps; c++) {
      if (((1 << c) & nir_intrinsic_write_mask(instr)) == 0)
         continue;

      brw_reg src_comp = offset(src, bld, c);

      for (unsigned q = 0; q < bld.dispatch_width() / 8; q++) {
         fs_builder bld8 = bld.group(8, q);

         /* offset is always positive, so signedness doesn't matter */
         assert(offset_src.type == BRW_TYPE_D ||
                offset_src.type == BRW_TYPE_UD);

         brw_reg off =
            bld8.ADD(quarter(retype(offset_src, BRW_TYPE_UD), q),
                     brw_imm_ud(c + base_in_dwords));
         brw_reg m = bld8.AND(off, brw_imm_ud(0x3));
         brw_reg t = bld8.SHL(bld8.MOV(brw_imm_ud(1)), m);
         brw_reg mask = bld8.SHL(t, brw_imm_ud(16));
         brw_reg final_offset = bld8.SHR(off, brw_imm_ud(2));

         brw_reg payload_srcs[4];
         unsigned length = 0;

         for (unsigned j = 0; j < 4; j++)
            payload_srcs[length++] = quarter(src_comp, q);

         brw_reg srcs[URB_LOGICAL_NUM_SRCS];
         srcs[URB_LOGICAL_SRC_HANDLE] = urb_handle;
         srcs[URB_LOGICAL_SRC_PER_SLOT_OFFSETS] = final_offset;
         srcs[URB_LOGICAL_SRC_CHANNEL_MASK] = mask;
         srcs[URB_LOGICAL_SRC_DATA] = brw_vgrf(bld.shader->alloc.allocate(length),
                                               BRW_TYPE_F);
         srcs[URB_LOGICAL_SRC_COMPONENTS] = brw_imm_ud(length);
         bld8.LOAD_PAYLOAD(srcs[URB_LOGICAL_SRC_DATA], payload_srcs, length, 0);

         fs_inst *inst = bld8.emit(SHADER_OPCODE_URB_WRITE_LOGICAL,
                                   reg_undef, srcs, ARRAY_SIZE(srcs));
         inst->offset = 0;
      }
   }
}

static void
emit_urb_direct_reads(const fs_builder &bld, nir_intrinsic_instr *instr,
                      const brw_reg &dest, brw_reg urb_handle)
{
   assert(instr->def.bit_size == 32);

   unsigned comps = instr->def.num_components;
   if (comps == 0)
      return;

   nir_src *offset_nir_src = nir_get_io_offset_src(instr);
   assert(nir_src_is_const(*offset_nir_src));

   const unsigned offset_in_dwords = nir_intrinsic_base(instr) +
                                     nir_src_as_uint(*offset_nir_src) +
                                     component_from_intrinsic(instr);

   unsigned urb_global_offset = offset_in_dwords / 4;
   adjust_handle_and_offset(bld, urb_handle, urb_global_offset);

   const unsigned comp_offset = offset_in_dwords % 4;
   const unsigned num_regs = comp_offset + comps;

   fs_builder ubld8 = bld.group(8, 0).exec_all();
   brw_reg data = ubld8.vgrf(BRW_TYPE_UD, num_regs);
   brw_reg srcs[URB_LOGICAL_NUM_SRCS];
   srcs[URB_LOGICAL_SRC_HANDLE] = urb_handle;

   fs_inst *inst = ubld8.emit(SHADER_OPCODE_URB_READ_LOGICAL, data,
                              srcs, ARRAY_SIZE(srcs));
   inst->offset = urb_global_offset;
   assert(inst->offset < 2048);
   inst->size_written = num_regs * REG_SIZE;

   for (unsigned c = 0; c < comps; c++) {
      brw_reg dest_comp = offset(dest, bld, c);
      brw_reg data_comp = horiz_stride(offset(data, ubld8, comp_offset + c), 0);
      bld.MOV(retype(dest_comp, BRW_TYPE_UD), data_comp);
   }
}

static void
emit_urb_direct_reads_xe2(const fs_builder &bld, nir_intrinsic_instr *instr,
                          const brw_reg &dest, brw_reg urb_handle)
{
   assert(instr->def.bit_size == 32);

   unsigned comps = instr->def.num_components;
   if (comps == 0)
      return;

   nir_src *offset_nir_src = nir_get_io_offset_src(instr);
   assert(nir_src_is_const(*offset_nir_src));

   fs_builder ubld16 = bld.group(16, 0).exec_all();

   const unsigned offset_in_dwords = nir_intrinsic_base(instr) +
                                     nir_src_as_uint(*offset_nir_src) +
                                     component_from_intrinsic(instr);

   if (offset_in_dwords > 0)
      urb_handle = ubld16.ADD(urb_handle, brw_imm_ud(offset_in_dwords * 4));

   brw_reg data = ubld16.vgrf(BRW_TYPE_UD, comps);
   brw_reg srcs[URB_LOGICAL_NUM_SRCS];
   srcs[URB_LOGICAL_SRC_HANDLE] = urb_handle;

   fs_inst *inst = ubld16.emit(SHADER_OPCODE_URB_READ_LOGICAL,
                               data, srcs, ARRAY_SIZE(srcs));
   inst->size_written = 2 * comps * REG_SIZE;

   for (unsigned c = 0; c < comps; c++) {
      brw_reg dest_comp = offset(dest, bld, c);
      brw_reg data_comp = horiz_stride(offset(data, ubld16, c), 0);
      bld.MOV(retype(dest_comp, BRW_TYPE_UD), data_comp);
   }
}

static void
emit_urb_indirect_reads(const fs_builder &bld, nir_intrinsic_instr *instr,
                        const brw_reg &dest, const brw_reg &offset_src, brw_reg urb_handle)
{
   assert(instr->def.bit_size == 32);

   unsigned comps = instr->def.num_components;
   if (comps == 0)
      return;

   brw_reg seq_ud;
   {
      fs_builder ubld8 = bld.group(8, 0).exec_all();
      seq_ud = ubld8.vgrf(BRW_TYPE_UD, 1);
      brw_reg seq_uw = ubld8.vgrf(BRW_TYPE_UW, 1);
      ubld8.MOV(seq_uw, brw_reg(brw_imm_v(0x76543210)));
      ubld8.MOV(seq_ud, seq_uw);
      seq_ud = ubld8.SHL(seq_ud, brw_imm_ud(2));
   }

   const unsigned base_in_dwords = nir_intrinsic_base(instr) +
                                   component_from_intrinsic(instr);

   for (unsigned c = 0; c < comps; c++) {
      for (unsigned q = 0; q < bld.dispatch_width() / 8; q++) {
         fs_builder bld8 = bld.group(8, q);

         /* offset is always positive, so signedness doesn't matter */
         assert(offset_src.type == BRW_TYPE_D ||
                offset_src.type == BRW_TYPE_UD);
         brw_reg off =
            bld8.ADD(bld8.MOV(quarter(retype(offset_src, BRW_TYPE_UD), q)),
                     brw_imm_ud(base_in_dwords + c));

         STATIC_ASSERT(IS_POT(REG_SIZE) && REG_SIZE > 1);

         brw_reg comp;
         comp = bld8.AND(off, brw_imm_ud(0x3));
         comp = bld8.SHL(comp, brw_imm_ud(ffs(REG_SIZE) - 1));
         comp = bld8.ADD(comp, seq_ud);

         off = bld8.SHR(off, brw_imm_ud(2));

         brw_reg srcs[URB_LOGICAL_NUM_SRCS];
         srcs[URB_LOGICAL_SRC_HANDLE] = urb_handle;
         srcs[URB_LOGICAL_SRC_PER_SLOT_OFFSETS] = off;

         brw_reg data = bld8.vgrf(BRW_TYPE_UD, 4);

         fs_inst *inst = bld8.emit(SHADER_OPCODE_URB_READ_LOGICAL,
                                   data, srcs, ARRAY_SIZE(srcs));
         inst->offset = 0;
         inst->size_written = 4 * REG_SIZE;

         brw_reg dest_comp = offset(dest, bld, c);
         bld8.emit(SHADER_OPCODE_MOV_INDIRECT,
                   retype(quarter(dest_comp, q), BRW_TYPE_UD),
                   data,
                   comp,
                   brw_imm_ud(4 * REG_SIZE));
      }
   }
}

static void
emit_urb_indirect_reads_xe2(const fs_builder &bld, nir_intrinsic_instr *instr,
                            const brw_reg &dest, const brw_reg &offset_src,
                            brw_reg urb_handle)
{
   assert(instr->def.bit_size == 32);

   unsigned comps = instr->def.num_components;
   if (comps == 0)
      return;

   fs_builder ubld16 = bld.group(16, 0).exec_all();

   const unsigned offset_in_dwords = nir_intrinsic_base(instr) +
                                     component_from_intrinsic(instr);

   if (offset_in_dwords > 0)
      urb_handle = ubld16.ADD(urb_handle, brw_imm_ud(offset_in_dwords * 4));

   brw_reg data = ubld16.vgrf(BRW_TYPE_UD, comps);

   for (unsigned q = 0; q < bld.dispatch_width() / 16; q++) {
      fs_builder wbld = bld.group(16, q);

      brw_reg addr = wbld.SHL(retype(horiz_offset(offset_src, 16 * q),
                                     BRW_TYPE_UD),
                              brw_imm_ud(2));

      brw_reg srcs[URB_LOGICAL_NUM_SRCS];
      srcs[URB_LOGICAL_SRC_HANDLE] = wbld.ADD(addr, urb_handle);

      fs_inst *inst = wbld.emit(SHADER_OPCODE_URB_READ_LOGICAL,
                                 data, srcs, ARRAY_SIZE(srcs));
      inst->size_written = 2 * comps * REG_SIZE;

      for (unsigned c = 0; c < comps; c++) {
         brw_reg dest_comp = horiz_offset(offset(dest, bld, c), 16 * q);
         brw_reg data_comp = offset(data, wbld, c);
         wbld.MOV(retype(dest_comp, BRW_TYPE_UD), data_comp);
      }
   }
}

static void
emit_task_mesh_store(nir_to_brw_state &ntb,
                     const fs_builder &bld, nir_intrinsic_instr *instr,
                     const brw_reg &urb_handle)
{
   brw_reg src = get_nir_src(ntb, instr->src[0]);
   nir_src *offset_nir_src = nir_get_io_offset_src(instr);

   if (nir_src_is_const(*offset_nir_src)) {
      if (bld.shader->devinfo->ver >= 20)
         emit_urb_direct_writes_xe2(bld, instr, src, urb_handle);
      else
         emit_urb_direct_writes(bld, instr, src, urb_handle);
   } else {
      if (bld.shader->devinfo->ver >= 20) {
         emit_urb_indirect_writes_xe2(bld, instr, src, get_nir_src(ntb, *offset_nir_src), urb_handle);
         return;
      }
      bool use_mod = false;
      unsigned mod;

      /* Try to calculate the value of (offset + base) % 4. If we can do
       * this, then we can do indirect writes using only 1 URB write.
       */
      use_mod = nir_mod_analysis(nir_get_scalar(offset_nir_src->ssa, 0), nir_type_uint, 4, &mod);
      if (use_mod) {
         mod += nir_intrinsic_base(instr) + component_from_intrinsic(instr);
         mod %= 4;
      }

      if (use_mod) {
         emit_urb_indirect_writes_mod(bld, instr, src, get_nir_src(ntb, *offset_nir_src), urb_handle, mod);
      } else {
         emit_urb_indirect_writes(bld, instr, src, get_nir_src(ntb, *offset_nir_src), urb_handle);
      }
   }
}

static void
emit_task_mesh_load(nir_to_brw_state &ntb,
                    const fs_builder &bld, nir_intrinsic_instr *instr,
                    const brw_reg &urb_handle)
{
   brw_reg dest = get_nir_def(ntb, instr->def);
   nir_src *offset_nir_src = nir_get_io_offset_src(instr);

   /* TODO(mesh): for per_vertex and per_primitive, if we could keep around
    * the non-array-index offset, we could use to decide if we can perform
    * a single large aligned read instead one per component.
    */

   if (nir_src_is_const(*offset_nir_src)) {
      if (bld.shader->devinfo->ver >= 20)
         emit_urb_direct_reads_xe2(bld, instr, dest, urb_handle);
      else
         emit_urb_direct_reads(bld, instr, dest, urb_handle);
   } else {
      if (bld.shader->devinfo->ver >= 20)
         emit_urb_indirect_reads_xe2(bld, instr, dest, get_nir_src(ntb, *offset_nir_src), urb_handle);
      else
         emit_urb_indirect_reads(bld, instr, dest, get_nir_src(ntb, *offset_nir_src), urb_handle);
   }
}

static void
fs_nir_emit_task_mesh_intrinsic(nir_to_brw_state &ntb, const fs_builder &bld,
                                nir_intrinsic_instr *instr)
{
   fs_visitor &s = ntb.s;

   assert(s.stage == MESA_SHADER_MESH || s.stage == MESA_SHADER_TASK);
   const task_mesh_thread_payload &payload = s.task_mesh_payload();

   brw_reg dest;
   if (nir_intrinsic_infos[instr->intrinsic].has_dest)
      dest = get_nir_def(ntb, instr->def);

   switch (instr->intrinsic) {
   case nir_intrinsic_load_draw_id:
      dest = retype(dest, BRW_TYPE_UD);
      bld.MOV(dest, payload.extended_parameter_0);
      break;

   case nir_intrinsic_load_local_invocation_id:
      unreachable("local invocation id should have been lowered earlier");
      break;

   case nir_intrinsic_load_local_invocation_index:
      dest = retype(dest, BRW_TYPE_UD);
      bld.MOV(dest, payload.local_index);
      break;

   case nir_intrinsic_load_num_workgroups:
      dest = retype(dest, BRW_TYPE_UD);
      bld.MOV(offset(dest, bld, 0), brw_uw1_grf(0, 13)); /* g0.6 >> 16 */
      bld.MOV(offset(dest, bld, 1), brw_uw1_grf(0, 8));  /* g0.4 & 0xffff */
      bld.MOV(offset(dest, bld, 2), brw_uw1_grf(0, 9));  /* g0.4 >> 16 */
      break;

   case nir_intrinsic_load_workgroup_index:
      dest = retype(dest, BRW_TYPE_UD);
      bld.MOV(dest, retype(brw_vec1_grf(0, 1), BRW_TYPE_UD));
      break;

   default:
      fs_nir_emit_cs_intrinsic(ntb, instr);
      break;
   }
}

static void
fs_nir_emit_task_intrinsic(nir_to_brw_state &ntb,
                           nir_intrinsic_instr *instr)
{
   const fs_builder &bld = ntb.bld;
   fs_visitor &s = ntb.s;

   assert(s.stage == MESA_SHADER_TASK);
   const task_mesh_thread_payload &payload = s.task_mesh_payload();

   switch (instr->intrinsic) {
   case nir_intrinsic_store_output:
   case nir_intrinsic_store_task_payload:
      emit_task_mesh_store(ntb, bld, instr, payload.urb_output);
      break;

   case nir_intrinsic_load_output:
   case nir_intrinsic_load_task_payload:
      emit_task_mesh_load(ntb, bld, instr, payload.urb_output);
      break;

   default:
      fs_nir_emit_task_mesh_intrinsic(ntb, bld, instr);
      break;
   }
}

static void
fs_nir_emit_mesh_intrinsic(nir_to_brw_state &ntb,
                           nir_intrinsic_instr *instr)
{
   const fs_builder &bld = ntb.bld;
   fs_visitor &s = ntb.s;

   assert(s.stage == MESA_SHADER_MESH);
   const task_mesh_thread_payload &payload = s.task_mesh_payload();

   switch (instr->intrinsic) {
   case nir_intrinsic_store_per_primitive_output:
   case nir_intrinsic_store_per_vertex_output:
   case nir_intrinsic_store_output:
      emit_task_mesh_store(ntb, bld, instr, payload.urb_output);
      break;

   case nir_intrinsic_load_per_vertex_output:
   case nir_intrinsic_load_per_primitive_output:
   case nir_intrinsic_load_output:
      emit_task_mesh_load(ntb, bld, instr, payload.urb_output);
      break;

   case nir_intrinsic_load_task_payload:
      emit_task_mesh_load(ntb, bld, instr, payload.task_urb_input);
      break;

   default:
      fs_nir_emit_task_mesh_intrinsic(ntb, bld, instr);
      break;
   }
}

static void
fs_nir_emit_intrinsic(nir_to_brw_state &ntb,
                      const fs_builder &bld, nir_intrinsic_instr *instr)
{
   const intel_device_info *devinfo = ntb.devinfo;
   fs_visitor &s = ntb.s;

   /* We handle this as a special case */
   if (instr->intrinsic == nir_intrinsic_decl_reg) {
      assert(nir_intrinsic_num_array_elems(instr) == 0);
      unsigned bit_size = nir_intrinsic_bit_size(instr);
      unsigned num_components = nir_intrinsic_num_components(instr);
      const brw_reg_type reg_type =
         brw_type_with_size(bit_size == 8 ? BRW_TYPE_D : BRW_TYPE_F,
                            bit_size);

      /* Re-use the destination's slot in the table for the register */
      ntb.ssa_values[instr->def.index] =
         bld.vgrf(reg_type, num_components);
      return;
   }

   brw_reg dest;
   if (nir_intrinsic_infos[instr->intrinsic].has_dest)
      dest = get_nir_def(ntb, instr->def);

   switch (instr->intrinsic) {
   case nir_intrinsic_resource_intel:
      ntb.ssa_bind_infos[instr->def.index].valid = true;
      ntb.ssa_bind_infos[instr->def.index].bindless =
         (nir_intrinsic_resource_access_intel(instr) &
          nir_resource_intel_bindless) != 0;
      ntb.ssa_bind_infos[instr->def.index].block =
         nir_intrinsic_resource_block_intel(instr);
      ntb.ssa_bind_infos[instr->def.index].set =
         nir_intrinsic_desc_set(instr);
      ntb.ssa_bind_infos[instr->def.index].binding =
         nir_intrinsic_binding(instr);

      if (nir_intrinsic_resource_access_intel(instr) &
           nir_resource_intel_non_uniform) {
         ntb.uniform_values[instr->def.index] = brw_reg();
      } else {
         ntb.uniform_values[instr->def.index] =
            try_rebuild_source(ntb, bld, instr->src[1].ssa);
      }
      ntb.ssa_values[instr->def.index] =
         get_nir_src(ntb, instr->src[1]);
      break;

   case nir_intrinsic_load_reg:
   case nir_intrinsic_store_reg:
      /* Nothing to do with these. */
      break;

   case nir_intrinsic_load_global_constant_uniform_block_intel:
      ntb.uniform_values[instr->src[0].ssa->index] =
         try_rebuild_source(ntb, bld, instr->src[0].ssa, true);
      FALLTHROUGH;
   case nir_intrinsic_load_ssbo_uniform_block_intel:
   case nir_intrinsic_load_shared_uniform_block_intel:
   case nir_intrinsic_load_global_block_intel:
   case nir_intrinsic_store_global_block_intel:
   case nir_intrinsic_load_shared_block_intel:
   case nir_intrinsic_store_shared_block_intel:
   case nir_intrinsic_load_ssbo_block_intel:
   case nir_intrinsic_store_ssbo_block_intel:
   case nir_intrinsic_image_load:
   case nir_intrinsic_image_store:
   case nir_intrinsic_image_atomic:
   case nir_intrinsic_image_atomic_swap:
   case nir_intrinsic_bindless_image_load:
   case nir_intrinsic_bindless_image_store:
   case nir_intrinsic_bindless_image_atomic:
   case nir_intrinsic_bindless_image_atomic_swap:
   case nir_intrinsic_load_shared:
   case nir_intrinsic_store_shared:
   case nir_intrinsic_shared_atomic:
   case nir_intrinsic_shared_atomic_swap:
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_store_ssbo:
   case nir_intrinsic_ssbo_atomic:
   case nir_intrinsic_ssbo_atomic_swap:
   case nir_intrinsic_load_global:
   case nir_intrinsic_load_global_constant:
   case nir_intrinsic_store_global:
   case nir_intrinsic_global_atomic:
   case nir_intrinsic_global_atomic_swap:
   case nir_intrinsic_load_scratch:
   case nir_intrinsic_store_scratch:
      fs_nir_emit_memory_access(ntb, bld, instr);
      break;

   case nir_intrinsic_image_size:
   case nir_intrinsic_bindless_image_size: {
      /* Cube image sizes should have previously been lowered to a 2D array */
      assert(nir_intrinsic_image_dim(instr) != GLSL_SAMPLER_DIM_CUBE);

      /* Unlike the [un]typed load and store opcodes, the TXS that this turns
       * into will handle the binding table index for us in the geneerator.
       * Incidentally, this means that we can handle bindless with exactly the
       * same code.
       */
      brw_reg image = retype(get_nir_src_imm(ntb, instr->src[0]), BRW_TYPE_UD);
      image = bld.emit_uniformize(image);

      assert(nir_src_as_uint(instr->src[1]) == 0);

      brw_reg srcs[TEX_LOGICAL_NUM_SRCS];
      if (instr->intrinsic == nir_intrinsic_image_size)
         srcs[TEX_LOGICAL_SRC_SURFACE] = image;
      else
         srcs[TEX_LOGICAL_SRC_SURFACE_HANDLE] = image;
      srcs[TEX_LOGICAL_SRC_SAMPLER] = brw_imm_d(0);
      srcs[TEX_LOGICAL_SRC_COORD_COMPONENTS] = brw_imm_d(0);
      srcs[TEX_LOGICAL_SRC_GRAD_COMPONENTS] = brw_imm_d(0);
      srcs[TEX_LOGICAL_SRC_RESIDENCY] = brw_imm_d(0);

      /* Since the image size is always uniform, we can just emit a SIMD8
       * query instruction and splat the result out.
       */
      const fs_builder ubld = bld.exec_all().group(8 * reg_unit(devinfo), 0);

      brw_reg tmp = ubld.vgrf(BRW_TYPE_UD, 4);
      fs_inst *inst = ubld.emit(SHADER_OPCODE_IMAGE_SIZE_LOGICAL,
                                tmp, srcs, ARRAY_SIZE(srcs));
      inst->size_written = 4 * REG_SIZE * reg_unit(devinfo);

      for (unsigned c = 0; c < instr->def.num_components; ++c) {
         bld.MOV(offset(retype(dest, tmp.type), bld, c),
                 component(offset(tmp, ubld, c), 0));
      }
      break;
   }

   case nir_intrinsic_barrier:
   case nir_intrinsic_begin_invocation_interlock:
   case nir_intrinsic_end_invocation_interlock: {
      bool ugm_fence, slm_fence, tgm_fence, urb_fence;
      enum opcode opcode = BRW_OPCODE_NOP;

      /* Handling interlock intrinsics here will allow the logic for IVB
       * render cache (see below) to be reused.
       */

      switch (instr->intrinsic) {
      case nir_intrinsic_barrier: {
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
         assert(s.stage == MESA_SHADER_FRAGMENT);
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
         assert(s.stage == MESA_SHADER_FRAGMENT);
         ugm_fence = tgm_fence = true;
         slm_fence = urb_fence = false;
         opcode = SHADER_OPCODE_MEMORY_FENCE;
         break;

      default:
         unreachable("invalid intrinsic");
      }

      if (opcode == BRW_OPCODE_NOP)
         break;

      if (s.nir->info.shared_size > 0) {
         assert(gl_shader_stage_uses_workgroup(s.stage));
      } else {
         slm_fence = false;
      }

      /* If the workgroup fits in a single HW thread, the messages for SLM are
       * processed in-order and the shader itself is already synchronized so
       * the memory fence is not necessary.
       *
       * TODO: Check if applies for many HW threads sharing same Data Port.
       */
      if (!s.nir->info.workgroup_size_variable &&
          slm_fence && brw_workgroup_size(s) <= s.dispatch_width)
         slm_fence = false;

      switch (s.stage) {
         case MESA_SHADER_TESS_CTRL:
         case MESA_SHADER_TASK:
         case MESA_SHADER_MESH:
            break;
         default:
            urb_fence = false;
            break;
      }

      unsigned fence_regs_count = 0;
      brw_reg fence_regs[4] = {};

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
         ubld.exec_all().group(1, 0).SYNC(TGL_SYNC_ALLWR);
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
               ubld.exec_all().group(1, 0).SYNC(TGL_SYNC_ALLWR);
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
         /* Simulation also complains on Gfx9 if we do not enable commit.
          */
         const bool commit_enable =
            instr->intrinsic == nir_intrinsic_end_invocation_interlock ||
            devinfo->ver == 9;

         if (tgm_fence || ugm_fence || slm_fence || urb_fence) {
            fence_regs[fence_regs_count++] =
               emit_fence(ubld, opcode, GFX7_SFID_DATAPORT_DATA_CACHE, 0,
                          commit_enable, 0 /* BTI */);
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
      const brw_reg shader_clock = get_timestamp(bld);
      const brw_reg srcs[] = { component(shader_clock, 0),
                              component(shader_clock, 1) };
      bld.LOAD_PAYLOAD(dest, srcs, ARRAY_SIZE(srcs), 0);
      break;
   }

   case nir_intrinsic_load_reloc_const_intel: {
      uint32_t id = nir_intrinsic_param_idx(instr);
      uint32_t base = nir_intrinsic_base(instr);

      /* Emit the reloc in the smallest SIMD size to limit register usage. */
      const fs_builder ubld = bld.exec_all().group(1, 0);
      brw_reg small_dest = ubld.vgrf(dest.type);
      ubld.UNDEF(small_dest);
      ubld.exec_all().group(1, 0).emit(SHADER_OPCODE_MOV_RELOC_IMM, small_dest,
                                       brw_imm_ud(id), brw_imm_ud(base));

      /* Copy propagation will get rid of this MOV. */
      bld.MOV(dest, component(small_dest, 0));
      break;
   }

   case nir_intrinsic_load_uniform: {
      /* Offsets are in bytes but they should always aligned to
       * the type size
       */
      unsigned base_offset = nir_intrinsic_base(instr);
      assert(base_offset % 4 == 0 || base_offset % brw_type_size_bytes(dest.type) == 0);

      brw_reg src = brw_uniform_reg(base_offset / 4, dest.type);

      if (nir_src_is_const(instr->src[0])) {
         unsigned load_offset = nir_src_as_uint(instr->src[0]);
         assert(load_offset % brw_type_size_bytes(dest.type) == 0);
         /* The base offset can only handle 32-bit units, so for 16-bit
          * data take the modulo of the offset with 4 bytes and add it to
          * the offset to read from within the source register.
          */
         src.offset = load_offset + base_offset % 4;

         for (unsigned j = 0; j < instr->num_components; j++) {
            bld.MOV(offset(dest, bld, j), offset(src, bld, j));
         }
      } else {
         brw_reg indirect = retype(get_nir_src(ntb, instr->src[0]),
                                  BRW_TYPE_UD);

         /* We need to pass a size to the MOV_INDIRECT but we don't want it to
          * go past the end of the uniform.  In order to keep the n'th
          * component from running past, we subtract off the size of all but
          * one component of the vector.
          */
         assert(nir_intrinsic_range(instr) >=
                instr->num_components * brw_type_size_bytes(dest.type));
         unsigned read_size = nir_intrinsic_range(instr) -
            (instr->num_components - 1) * brw_type_size_bytes(dest.type);

         bool supports_64bit_indirects = !intel_device_info_is_9lp(devinfo);

         if (brw_type_size_bytes(dest.type) != 8 || supports_64bit_indirects) {
            for (unsigned j = 0; j < instr->num_components; j++) {
               bld.emit(SHADER_OPCODE_MOV_INDIRECT,
                        offset(dest, bld, j), offset(src, bld, j),
                        indirect, brw_imm_ud(read_size));
            }
         } else {
            const unsigned num_mov_indirects =
               brw_type_size_bytes(dest.type) / brw_type_size_bytes(BRW_TYPE_UD);
            /* We read a little bit less per MOV INDIRECT, as they are now
             * 32-bits ones instead of 64-bit. Fix read_size then.
             */
            const unsigned read_size_32bit = read_size -
                (num_mov_indirects - 1) * brw_type_size_bytes(BRW_TYPE_UD);
            for (unsigned j = 0; j < instr->num_components; j++) {
               for (unsigned i = 0; i < num_mov_indirects; i++) {
                  bld.emit(SHADER_OPCODE_MOV_INDIRECT,
                           subscript(offset(dest, bld, j), BRW_TYPE_UD, i),
                           subscript(offset(src, bld, j), BRW_TYPE_UD, i),
                           indirect, brw_imm_ud(read_size_32bit));
               }
            }
         }
      }
      break;
   }

   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_ubo_uniform_block_intel: {
      brw_reg surface, surface_handle;
      bool no_mask_handle = false;

      if (get_nir_src_bindless(ntb, instr->src[0]))
         surface_handle = get_nir_buffer_intrinsic_index(ntb, bld, instr, &no_mask_handle);
      else
         surface = get_nir_buffer_intrinsic_index(ntb, bld, instr, &no_mask_handle);

      if (!nir_src_is_const(instr->src[1])) {
         s.prog_data->has_ubo_pull = true;

         if (instr->intrinsic == nir_intrinsic_load_ubo) {
            /* load_ubo with non-uniform offset */
            brw_reg base_offset = retype(get_nir_src(ntb, instr->src[1]),
                                        BRW_TYPE_UD);

            const unsigned comps_per_load = brw_type_size_bytes(dest.type) == 8 ? 2 : 4;

            for (int i = 0; i < instr->num_components; i += comps_per_load) {
               const unsigned remaining = instr->num_components - i;
               bld.VARYING_PULL_CONSTANT_LOAD(offset(dest, bld, i),
                                              surface, surface_handle,
                                              base_offset,
                                              i * brw_type_size_bytes(dest.type),
                                              instr->def.bit_size / 8,
                                              MIN2(remaining, comps_per_load));
            }
         } else {
            /* load_ubo_uniform_block_intel with non-constant offset */
            fs_nir_emit_memory_access(ntb, bld, instr);
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
         const unsigned type_size = brw_type_size_bytes(dest.type);
         const unsigned load_offset = nir_src_as_uint(instr->src[1]);
         const unsigned ubo_block =
            brw_nir_ubo_surface_index_get_push_block(instr->src[0]);
         const unsigned offset_256b = load_offset / 32;
         const unsigned end_256b =
            DIV_ROUND_UP(load_offset + type_size * instr->num_components, 32);

         /* See if we've selected this as a push constant candidate */
         brw_reg push_reg;
         for (int i = 0; i < 4; i++) {
            const struct brw_ubo_range *range = &s.prog_data->ubo_ranges[i];
            if (range->block == ubo_block &&
                offset_256b >= range->start &&
                end_256b <= range->start + range->length) {

               push_reg = brw_uniform_reg(UBO_START + i, dest.type);
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

         s.prog_data->has_ubo_pull = true;

         const unsigned block_sz = 64; /* Fetch one cacheline at a time. */
         const fs_builder ubld = bld.exec_all().group(block_sz / 4, 0);

         for (unsigned c = 0; c < instr->num_components;) {
            const unsigned base = load_offset + c * type_size;
            /* Number of usable components in the next block-aligned load. */
            const unsigned count = MIN2(instr->num_components - c,
                                        (block_sz - base % block_sz) / type_size);

            const brw_reg packed_consts = ubld.vgrf(BRW_TYPE_UD);
            brw_reg srcs[PULL_UNIFORM_CONSTANT_SRCS];
            srcs[PULL_UNIFORM_CONSTANT_SRC_SURFACE]        = surface;
            srcs[PULL_UNIFORM_CONSTANT_SRC_SURFACE_HANDLE] = surface_handle;
            srcs[PULL_UNIFORM_CONSTANT_SRC_OFFSET]         = brw_imm_ud(base & ~(block_sz - 1));
            srcs[PULL_UNIFORM_CONSTANT_SRC_SIZE]           = brw_imm_ud(block_sz);

            ubld.emit(FS_OPCODE_UNIFORM_PULL_CONSTANT_LOAD, packed_consts,
                      srcs, PULL_UNIFORM_CONSTANT_SRCS);

            const brw_reg consts =
               retype(byte_offset(packed_consts, base & (block_sz - 1)),
                      dest.type);

            for (unsigned d = 0; d < count; d++)
               bld.MOV(offset(dest, bld, c + d), component(consts, d));

            c += count;
         }
      }
      break;
   }

   case nir_intrinsic_store_output: {
      assert(nir_src_bit_size(instr->src[0]) == 32);
      brw_reg src = get_nir_src(ntb, instr->src[0]);

      unsigned store_offset = nir_src_as_uint(instr->src[1]);
      unsigned num_components = instr->num_components;
      unsigned first_component = nir_intrinsic_component(instr);

      brw_reg new_dest = retype(offset(s.outputs[instr->const_index[0]], bld,
                                      4 * store_offset), src.type);

      brw_combine_with_vec(bld, offset(new_dest, bld, first_component),
                           src, num_components);
      break;
   }

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
      const fs_builder ubld = bld.exec_all().group(8 * reg_unit(devinfo), 0);
      brw_reg ret_payload = ubld.vgrf(BRW_TYPE_UD, 4);

      /* Set LOD = 0 */
      brw_reg src_payload = ubld.MOV(brw_imm_ud(0));

      brw_reg srcs[GET_BUFFER_SIZE_SRCS];
      srcs[get_nir_src_bindless(ntb, instr->src[0]) ?
           GET_BUFFER_SIZE_SRC_SURFACE_HANDLE :
           GET_BUFFER_SIZE_SRC_SURFACE] =
         get_nir_buffer_intrinsic_index(ntb, bld, instr);
      srcs[GET_BUFFER_SIZE_SRC_LOD] = src_payload;
      fs_inst *inst = ubld.emit(SHADER_OPCODE_GET_BUFFER_SIZE, ret_payload,
                                srcs, GET_BUFFER_SIZE_SRCS);
      inst->header_size = 0;
      inst->mlen = reg_unit(devinfo);
      inst->size_written = 4 * REG_SIZE * reg_unit(devinfo);

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
      brw_reg size_padding  = ubld.AND(ret_payload, brw_imm_ud(3));
      brw_reg size_aligned4 = ubld.AND(ret_payload, brw_imm_ud(~3));
      brw_reg buffer_size   = ubld.ADD(size_aligned4, negate(size_padding));

      bld.MOV(retype(dest, ret_payload.type), component(buffer_size, 0));
      break;
   }

   case nir_intrinsic_load_subgroup_size:
      /* This should only happen for fragment shaders because every other case
       * is lowered in NIR so we can optimize on it.
       */
      assert(s.stage == MESA_SHADER_FRAGMENT);
      bld.MOV(retype(dest, BRW_TYPE_D), brw_imm_d(s.dispatch_width));
      break;

   case nir_intrinsic_load_subgroup_invocation:
      bld.MOV(retype(dest, BRW_TYPE_UD), bld.LOAD_SUBGROUP_INVOCATION());
      break;

   case nir_intrinsic_load_subgroup_eq_mask:
   case nir_intrinsic_load_subgroup_ge_mask:
   case nir_intrinsic_load_subgroup_gt_mask:
   case nir_intrinsic_load_subgroup_le_mask:
   case nir_intrinsic_load_subgroup_lt_mask:
      unreachable("not reached");

   case nir_intrinsic_ddx_fine:
      bld.emit(FS_OPCODE_DDX_FINE, retype(dest, BRW_TYPE_F),
               retype(get_nir_src(ntb, instr->src[0]), BRW_TYPE_F));
      break;
   case nir_intrinsic_ddx:
   case nir_intrinsic_ddx_coarse:
      bld.emit(FS_OPCODE_DDX_COARSE, retype(dest, BRW_TYPE_F),
               retype(get_nir_src(ntb, instr->src[0]), BRW_TYPE_F));
      break;
   case nir_intrinsic_ddy_fine:
      bld.emit(FS_OPCODE_DDY_FINE, retype(dest, BRW_TYPE_F),
               retype(get_nir_src(ntb, instr->src[0]), BRW_TYPE_F));
      break;
   case nir_intrinsic_ddy:
   case nir_intrinsic_ddy_coarse:
      bld.emit(FS_OPCODE_DDY_COARSE, retype(dest, BRW_TYPE_F),
               retype(get_nir_src(ntb, instr->src[0]), BRW_TYPE_F));
      break;

   case nir_intrinsic_vote_any:
   case nir_intrinsic_vote_all:
   case nir_intrinsic_quad_vote_any:
   case nir_intrinsic_quad_vote_all: {
      const bool any = instr->intrinsic == nir_intrinsic_vote_any ||
                       instr->intrinsic == nir_intrinsic_quad_vote_any;
      const bool quad = instr->intrinsic == nir_intrinsic_quad_vote_any ||
                        instr->intrinsic == nir_intrinsic_quad_vote_all;

      brw_reg cond = get_nir_src(ntb, instr->src[0]);
      const unsigned cluster_size = quad ? 4 : s.dispatch_width;

      bld.emit(any ? SHADER_OPCODE_VOTE_ANY : SHADER_OPCODE_VOTE_ALL,
               retype(dest, BRW_TYPE_UD), cond, brw_imm_ud(cluster_size));

      break;
   }

   case nir_intrinsic_vote_feq:
   case nir_intrinsic_vote_ieq: {
      brw_reg value = get_nir_src(ntb, instr->src[0]);
      if (instr->intrinsic == nir_intrinsic_vote_feq) {
         const unsigned bit_size = nir_src_bit_size(instr->src[0]);
         value.type = bit_size == 8 ? BRW_TYPE_B :
            brw_type_with_size(BRW_TYPE_F, bit_size);
      }
      bld.emit(SHADER_OPCODE_VOTE_EQUAL, retype(dest, BRW_TYPE_D), value);
      break;
   }

   case nir_intrinsic_ballot: {
      if (instr->def.bit_size > 32) {
         dest.type = BRW_TYPE_UQ;
      } else {
         dest.type = BRW_TYPE_UD;
      }

      /* Implement a fast-path for ballot(true). */
      if (nir_src_is_const(instr->src[0]) &&
          nir_src_as_bool(instr->src[0])) {
         brw_reg tmp = bld.vgrf(BRW_TYPE_UD);
         bld.exec_all().emit(SHADER_OPCODE_LOAD_LIVE_CHANNELS, tmp);
         bld.MOV(dest, brw_reg(component(tmp, 0)));
         break;
      }

      const brw_reg value = retype(get_nir_src(ntb, instr->src[0]),
                                  BRW_TYPE_UD);
      struct brw_reg flag = brw_flag_reg(0, 0);

      if (s.dispatch_width == 32)
         flag.type = BRW_TYPE_UD;

      bld.exec_all().group(1, 0).MOV(flag, retype(brw_imm_ud(0u), flag.type));
      bld.CMP(bld.null_reg_ud(), value, brw_imm_ud(0u), BRW_CONDITIONAL_NZ);
      bld.MOV(dest, flag);
      break;
   }

   case nir_intrinsic_read_invocation: {
      const brw_reg value = get_nir_src(ntb, instr->src[0]);
      const brw_reg invocation = get_nir_src_imm(ntb, instr->src[1]);

      if (invocation.file == IMM) {
         unsigned i = invocation.ud & (bld.dispatch_width() - 1);
         bld.MOV(retype(dest, value.type), component(value, i));
         break;
      }


      /* When for some reason the subgroup_size picked by NIR is larger than
       * the dispatch size picked by the backend (this could happen in RT,
       * FS), bound the invocation to the dispatch size.
       */
      brw_reg bound_invocation = retype(invocation, BRW_TYPE_UD);
      if (s.api_subgroup_size == 0 ||
          bld.dispatch_width() < s.api_subgroup_size) {
         bound_invocation =
            bld.AND(bound_invocation, brw_imm_ud(s.dispatch_width - 1));
      }

      brw_reg tmp = bld.BROADCAST(value, bld.emit_uniformize(bound_invocation));

      bld.MOV(retype(dest, value.type), tmp);
      break;
   }

   case nir_intrinsic_read_first_invocation: {
      const brw_reg value = get_nir_src(ntb, instr->src[0]);
      bld.MOV(retype(dest, value.type), bld.emit_uniformize(value));
      break;
   }

   case nir_intrinsic_shuffle: {
      const brw_reg value = get_nir_src(ntb, instr->src[0]);
      const brw_reg index = get_nir_src(ntb, instr->src[1]);

      bld.emit(SHADER_OPCODE_SHUFFLE, retype(dest, value.type), value, index);
      break;
   }

   case nir_intrinsic_first_invocation: {
      brw_reg tmp = bld.vgrf(BRW_TYPE_UD);
      bld.exec_all().emit(SHADER_OPCODE_FIND_LIVE_CHANNEL, tmp);
      bld.MOV(retype(dest, BRW_TYPE_UD),
              brw_reg(component(tmp, 0)));
      break;
   }

   case nir_intrinsic_last_invocation: {
      brw_reg tmp = bld.vgrf(BRW_TYPE_UD);
      bld.exec_all().emit(SHADER_OPCODE_FIND_LAST_LIVE_CHANNEL, tmp);
      bld.MOV(retype(dest, BRW_TYPE_UD),
              brw_reg(component(tmp, 0)));
      break;
   }

   case nir_intrinsic_quad_broadcast: {
      const brw_reg value = get_nir_src(ntb, instr->src[0]);
      const unsigned index = nir_src_as_uint(instr->src[1]);

      bld.emit(SHADER_OPCODE_CLUSTER_BROADCAST, retype(dest, value.type),
               value, brw_imm_ud(index), brw_imm_ud(4));
      break;
   }

   case nir_intrinsic_quad_swap_horizontal: {
      const brw_reg value = get_nir_src(ntb, instr->src[0]);
      const brw_reg tmp = bld.vgrf(value.type);

      const fs_builder ubld = bld.exec_all().group(s.dispatch_width / 2, 0);

      const brw_reg src_left = horiz_stride(value, 2);
      const brw_reg src_right = horiz_stride(horiz_offset(value, 1), 2);
      const brw_reg tmp_left = horiz_stride(tmp, 2);
      const brw_reg tmp_right = horiz_stride(horiz_offset(tmp, 1), 2);

      ubld.MOV(tmp_left, src_right);
      ubld.MOV(tmp_right, src_left);

      bld.MOV(retype(dest, value.type), tmp);
      break;
   }

   case nir_intrinsic_quad_swap_vertical: {
      const brw_reg value = get_nir_src(ntb, instr->src[0]);
      if (nir_src_bit_size(instr->src[0]) == 32) {
         /* For 32-bit, we can use a SIMD4x2 instruction to do this easily */
         const brw_reg tmp = bld.vgrf(value.type);
         const fs_builder ubld = bld.exec_all();
         ubld.emit(SHADER_OPCODE_QUAD_SWIZZLE, tmp, value,
                   brw_imm_ud(BRW_SWIZZLE4(2,3,0,1)));
         bld.MOV(retype(dest, value.type), tmp);
      } else {
         /* For larger data types, we have to either emit dispatch_width many
          * MOVs or else fall back to doing indirects.
          */
         brw_reg idx = bld.vgrf(BRW_TYPE_W);
         bld.XOR(idx, bld.LOAD_SUBGROUP_INVOCATION(), brw_imm_w(0x2));
         bld.emit(SHADER_OPCODE_SHUFFLE, retype(dest, value.type), value, idx);
      }
      break;
   }

   case nir_intrinsic_quad_swap_diagonal: {
      const brw_reg value = get_nir_src(ntb, instr->src[0]);
      if (nir_src_bit_size(instr->src[0]) == 32) {
         /* For 32-bit, we can use a SIMD4x2 instruction to do this easily */
         const brw_reg tmp = bld.vgrf(value.type);
         const fs_builder ubld = bld.exec_all();
         ubld.emit(SHADER_OPCODE_QUAD_SWIZZLE, tmp, value,
                   brw_imm_ud(BRW_SWIZZLE4(3,2,1,0)));
         bld.MOV(retype(dest, value.type), tmp);
      } else {
         /* For larger data types, we have to either emit dispatch_width many
          * MOVs or else fall back to doing indirects.
          */
         brw_reg idx = bld.vgrf(BRW_TYPE_W);
         bld.XOR(idx, bld.LOAD_SUBGROUP_INVOCATION(), brw_imm_w(0x3));
         bld.emit(SHADER_OPCODE_SHUFFLE, retype(dest, value.type), value, idx);
      }
      break;
   }

   case nir_intrinsic_reduce: {
      brw_reg src = get_nir_src(ntb, instr->src[0]);
      nir_op op = (nir_op)nir_intrinsic_reduction_op(instr);
      enum brw_reduce_op brw_op = brw_reduce_op_for_nir_reduction_op(op);
      unsigned cluster_size = nir_intrinsic_cluster_size(instr);
      if (cluster_size == 0 || cluster_size > s.dispatch_width)
         cluster_size = s.dispatch_width;

      /* Figure out the source type */
      src.type = brw_type_for_nir_type(devinfo,
         (nir_alu_type)(nir_op_infos[op].input_types[0] |
                        nir_src_bit_size(instr->src[0])));

      bld.emit(SHADER_OPCODE_REDUCE, retype(dest, src.type), src,
               brw_imm_ud(brw_op), brw_imm_ud(cluster_size));
      break;
   }

   case nir_intrinsic_inclusive_scan:
   case nir_intrinsic_exclusive_scan: {
      brw_reg src = get_nir_src(ntb, instr->src[0]);
      nir_op op = (nir_op)nir_intrinsic_reduction_op(instr);
      enum brw_reduce_op brw_op = brw_reduce_op_for_nir_reduction_op(op);

      /* Figure out the source type */
      src.type = brw_type_for_nir_type(devinfo,
         (nir_alu_type)(nir_op_infos[op].input_types[0] |
                        nir_src_bit_size(instr->src[0])));

      enum opcode opcode = instr->intrinsic == nir_intrinsic_exclusive_scan ?
            SHADER_OPCODE_EXCLUSIVE_SCAN : SHADER_OPCODE_INCLUSIVE_SCAN;

      bld.emit(opcode, retype(dest, src.type), src, brw_imm_ud(brw_op));
      break;
   }

   case nir_intrinsic_load_topology_id_intel: {
      /* These move around basically every hardware generation, so don't
       * do any unbounded checks and fail if the platform hasn't explicitly
       * been enabled here.
       */
      assert(devinfo->ver >= 12 && devinfo->ver <= 20);

      /* Here is what the layout of SR0 looks like on Gfx12
       * https://gfxspecs.intel.com/Predator/Home/Index/47256
       *   [13:11] : Slice ID.
       *   [10:9]  : Dual-SubSlice ID
       *   [8]     : SubSlice ID
       *   [7]     : EUID[2] (aka EU Row ID)
       *   [6]     : Reserved
       *   [5:4]   : EUID[1:0]
       *   [2:0]   : Thread ID
       *
       * Xe2: Engine 3D and GPGPU Programs, EU Overview, Registers and
       * Register Regions, ARF Registers, State Register,
       * https://gfxspecs.intel.com/Predator/Home/Index/56623
       *   [15:11] : Slice ID.
       *   [9:8]   : SubSlice ID
       *   [6:4]   : EUID
       *   [2:0]   : Thread ID
       */
      brw_reg raw_id = bld.vgrf(BRW_TYPE_UD);
      bld.UNDEF(raw_id);
      bld.emit(SHADER_OPCODE_READ_ARCH_REG, raw_id, retype(brw_sr0_reg(0),
                                                           BRW_TYPE_UD));
      switch (nir_intrinsic_base(instr)) {
      case BRW_TOPOLOGY_ID_DSS:
         if (devinfo->ver >= 20) {
            /* Xe2+: 3D and GPGPU Programs, Shared Functions, Ray Tracing:
             * https://gfxspecs.intel.com/Predator/Home/Index/56936
             *
             * Note: DSSID in all formulas below is a logical identifier of an
             * XeCore (a value that goes from 0 to (number_of_slices *
             * number_of_XeCores_per_slice -1). SW can get this value from
             * either:
             *
             *  - Message Control Register LogicalSSID field (only in shaders
             *    eligible for Mid-Thread Preemption).
             *  - Calculated based of State Register with the following formula:
             *    DSSID = StateRegister.SliceID * GT_ARCH_SS_PER_SLICE +
             *    StateRRegister.SubSliceID where GT_SS_PER_SLICE is an
             *    architectural parameter defined per product SKU.
             *
             * We are using the state register to calculate the DSSID.
             */
            brw_reg slice_id =
               bld.SHR(bld.AND(raw_id, brw_imm_ud(INTEL_MASK(15, 11))),
                       brw_imm_ud(11));

            /* Assert that max subslices covers at least 2 bits that we use for
             * subslices.
             */
            unsigned slice_stride = devinfo->max_subslices_per_slice;
            assert(slice_stride >= (1 << 2));
            brw_reg subslice_id =
               bld.SHR(bld.AND(raw_id, brw_imm_ud(INTEL_MASK(9, 8))),
                       brw_imm_ud(8));
            bld.ADD(retype(dest, BRW_TYPE_UD),
                    bld.MUL(slice_id, brw_imm_ud(slice_stride)), subslice_id);
         } else {
            /* Get rid of anything below dualsubslice */
            bld.SHR(retype(dest, BRW_TYPE_UD),
                    bld.AND(raw_id, brw_imm_ud(0x3fff)), brw_imm_ud(9));
         }
         break;
      case BRW_TOPOLOGY_ID_EU_THREAD_SIMD: {
         s.limit_dispatch_width(16, "Topology helper for Ray queries, "
                              "not supported in SIMD32 mode.");
         brw_reg dst = retype(dest, BRW_TYPE_UD);
         brw_reg eu;

         if (devinfo->ver >= 20) {
            /* Xe2+: Graphics Engine, 3D and GPGPU Programs, Shared Functions
             * Ray Tracing,
             * https://gfxspecs.intel.com/Predator/Home/Index/56936
             *
             * SyncStackID = (EUID[2:0] <<  8) | (ThreadID[2:0] << 4) |
             *               SIMDLaneID[3:0];
             *
             * This section just deals with the EUID part.
             *
             * The 3bit EU[2:0] we need to build for ray query memory addresses
             * computations is a bit odd :
             *
             *   EU[2:0] = raw_id[6:4] (identified as EUID[2:0])
             */
            eu = bld.SHL(bld.AND(raw_id, brw_imm_ud(INTEL_MASK(6, 4))),
                         brw_imm_ud(4));
         } else {
            /* EU[3:0] << 7
             *
             * The 4bit EU[3:0] we need to build for ray query memory addresses
             * computations is a bit odd :
             *
             *   EU[1:0] = raw_id[5:4] (identified as EUID[1:0])
             *   EU[2]   = raw_id[8]   (identified as SubSlice ID)
             *   EU[3]   = raw_id[7]   (identified as EUID[2] or Row ID)
             */
            brw_reg raw5_4 = bld.AND(raw_id, brw_imm_ud(INTEL_MASK(5, 4)));
            brw_reg raw7   = bld.AND(raw_id, brw_imm_ud(INTEL_MASK(7, 7)));
            brw_reg raw8   = bld.AND(raw_id, brw_imm_ud(INTEL_MASK(8, 8)));
            eu = bld.OR(bld.SHL(raw5_4, brw_imm_ud(3)),
                        bld.OR(bld.SHL(raw7, brw_imm_ud(3)),
                               bld.SHL(raw8, brw_imm_ud(1))));
         }

         /* ThreadID[2:0] << 4 (ThreadID comes from raw_id[2:0]) */
         brw_reg tid =
            bld.SHL(bld.AND(raw_id, brw_imm_ud(INTEL_MASK(2, 0))),
                    brw_imm_ud(4));

         /* LaneID[0:3] << 0 (Use subgroup invocation) */
         assert(bld.dispatch_width() <= 16); /* Limit to 4 bits */
         bld.ADD(dst, bld.OR(eu, tid), bld.LOAD_SUBGROUP_INVOCATION());
         break;
      }
      default:
         unreachable("Invalid topology id type");
      }
      break;
   }

   case nir_intrinsic_load_btd_stack_id_intel:
      if (s.stage == MESA_SHADER_COMPUTE) {
         assert(brw_cs_prog_data(s.prog_data)->uses_btd_stack_ids);
      } else {
         assert(brw_shader_stage_is_bindless(s.stage));
      }
      /* Stack IDs are always in R1 regardless of whether we're coming from a
       * bindless shader or a regular compute shader.
       */
      bld.MOV(retype(dest, BRW_TYPE_UD),
              retype(brw_vec8_grf(1 * reg_unit(devinfo), 0), BRW_TYPE_UW));
      break;

   case nir_intrinsic_btd_spawn_intel:
      if (s.stage == MESA_SHADER_COMPUTE) {
         assert(brw_cs_prog_data(s.prog_data)->uses_btd_stack_ids);
      } else {
         assert(brw_shader_stage_is_bindless(s.stage));
      }
      /* Make sure all the pointers to resume shaders have landed where other
       * threads can see them.
       */
      emit_rt_lsc_fence(bld, LSC_FENCE_LOCAL, LSC_FLUSH_TYPE_NONE);

      bld.emit(SHADER_OPCODE_BTD_SPAWN_LOGICAL, bld.null_reg_ud(),
               bld.emit_uniformize(get_nir_src(ntb, instr->src[0])),
               get_nir_src(ntb, instr->src[1]));
      break;

   case nir_intrinsic_btd_retire_intel:
      if (s.stage == MESA_SHADER_COMPUTE) {
         assert(brw_cs_prog_data(s.prog_data)->uses_btd_stack_ids);
      } else {
         assert(brw_shader_stage_is_bindless(s.stage));
      }
      /* Make sure all the pointers to resume shaders have landed where other
       * threads can see them.
       */
      emit_rt_lsc_fence(bld, LSC_FENCE_LOCAL, LSC_FLUSH_TYPE_NONE);
      bld.emit(SHADER_OPCODE_BTD_RETIRE_LOGICAL);
      break;

   case nir_intrinsic_trace_ray_intel: {
      const bool synchronous = nir_intrinsic_synchronous(instr);
      assert(brw_shader_stage_is_bindless(s.stage) || synchronous);

      /* Make sure all the previous RT structure writes are visible to the RT
       * fixed function within the DSS, as well as stack pointers to resume
       * shaders.
       */
      emit_rt_lsc_fence(bld, LSC_FENCE_LOCAL, LSC_FLUSH_TYPE_NONE);

      brw_reg srcs[RT_LOGICAL_NUM_SRCS];

      brw_reg globals = get_nir_src(ntb, instr->src[0]);
      srcs[RT_LOGICAL_SRC_GLOBALS] = bld.emit_uniformize(globals);
      srcs[RT_LOGICAL_SRC_BVH_LEVEL] = get_nir_src(ntb, instr->src[1]);
      srcs[RT_LOGICAL_SRC_TRACE_RAY_CONTROL] = get_nir_src(ntb, instr->src[2]);
      srcs[RT_LOGICAL_SRC_SYNCHRONOUS] = brw_imm_ud(synchronous);

      /* Bspec 57508: Structure_SIMD16TraceRayMessage:: RayQuery Enable
       *
       *    "When this bit is set in the header, Trace Ray Message behaves like
       *    a Ray Query. This message requires a write-back message indicating
       *    RayQuery for all valid Rays (SIMD lanes) have completed."
       */
      brw_reg dst = (devinfo->ver >= 20 && synchronous) ?
                    bld.vgrf(BRW_TYPE_UD) :
                    bld.null_reg_ud();

      bld.emit(RT_OPCODE_TRACE_RAY_LOGICAL, dst, srcs, RT_LOGICAL_NUM_SRCS);

      /* There is no actual value to use in the destination register of the
       * synchronous trace instruction. All of the communication with the HW
       * unit happens through memory reads/writes. So to ensure that the
       * operation has completed before we go read the results in memory, we
       * need a barrier followed by an invalidate before accessing memory.
       */
      if (synchronous) {
         bld.SYNC(TGL_SYNC_ALLWR);
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

static enum lsc_data_size
lsc_bits_to_data_size(unsigned bit_size)
{
   switch (bit_size / 8) {
   case 1:  return LSC_DATA_SIZE_D8U32;
   case 2:  return LSC_DATA_SIZE_D16U32;
   case 4:  return LSC_DATA_SIZE_D32;
   case 8:  return LSC_DATA_SIZE_D64;
   default:
      unreachable("Unsupported data size.");
   }
}

static void
fs_nir_emit_memory_access(nir_to_brw_state &ntb,
                          const fs_builder &bld,
                          nir_intrinsic_instr *instr)
{
   const intel_device_info *devinfo = ntb.devinfo;
   fs_visitor &s = ntb.s;

   brw_reg srcs[MEMORY_LOGICAL_NUM_SRCS];

   /* Start with some default values for most cases */

   enum lsc_opcode op = lsc_op_for_nir_intrinsic(instr);
   const bool is_store = !nir_intrinsic_infos[instr->intrinsic].has_dest;
   const bool is_atomic = lsc_opcode_is_atomic(op);
   const bool is_load = !is_store && !is_atomic;
   const bool include_helpers = nir_intrinsic_has_access(instr) &&
      (nir_intrinsic_access(instr) & ACCESS_INCLUDE_HELPERS);
   const unsigned align =
      nir_intrinsic_has_align(instr) ? nir_intrinsic_align(instr) : 0;
   bool no_mask_handle = false;
   int data_src = -1;

   srcs[MEMORY_LOGICAL_OPCODE] = brw_imm_ud(op);
   /* BINDING_TYPE, BINDING, and ADDRESS are handled in the switch */
   srcs[MEMORY_LOGICAL_COORD_COMPONENTS] = brw_imm_ud(1);
   srcs[MEMORY_LOGICAL_ALIGNMENT] = brw_imm_ud(align);
   /* DATA_SIZE and CHANNELS are handled below the switch */
   srcs[MEMORY_LOGICAL_FLAGS] =
      brw_imm_ud(include_helpers ? MEMORY_FLAG_INCLUDE_HELPERS : 0);
   /* DATA0 and DATA1 are handled below */

   switch (instr->intrinsic) {
   case nir_intrinsic_bindless_image_load:
   case nir_intrinsic_bindless_image_store:
   case nir_intrinsic_bindless_image_atomic:
   case nir_intrinsic_bindless_image_atomic_swap:
      srcs[MEMORY_LOGICAL_BINDING_TYPE] = brw_imm_ud(LSC_ADDR_SURFTYPE_BSS);
      FALLTHROUGH;
   case nir_intrinsic_image_load:
   case nir_intrinsic_image_store:
   case nir_intrinsic_image_atomic:
   case nir_intrinsic_image_atomic_swap:
      srcs[MEMORY_LOGICAL_MODE] = brw_imm_ud(MEMORY_MODE_TYPED);
      srcs[MEMORY_LOGICAL_BINDING] =
         get_nir_image_intrinsic_image(ntb, bld, instr);

      if (srcs[MEMORY_LOGICAL_BINDING_TYPE].file == BAD_FILE)
         srcs[MEMORY_LOGICAL_BINDING_TYPE] = brw_imm_ud(LSC_ADDR_SURFTYPE_BTI);

      srcs[MEMORY_LOGICAL_ADDRESS] = get_nir_src(ntb, instr->src[1]);
      srcs[MEMORY_LOGICAL_COORD_COMPONENTS] =
         brw_imm_ud(nir_image_intrinsic_coord_components(instr));

      data_src = 3;
      break;

   case nir_intrinsic_load_ubo_uniform_block_intel:
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_store_ssbo:
   case nir_intrinsic_ssbo_atomic:
   case nir_intrinsic_ssbo_atomic_swap:
   case nir_intrinsic_load_ssbo_block_intel:
   case nir_intrinsic_store_ssbo_block_intel:
   case nir_intrinsic_load_ssbo_uniform_block_intel:
      srcs[MEMORY_LOGICAL_MODE] = brw_imm_ud(MEMORY_MODE_UNTYPED);
      srcs[MEMORY_LOGICAL_BINDING_TYPE] =
         brw_imm_ud(get_nir_src_bindless(ntb, instr->src[is_store ? 1 : 0]) ?
                    LSC_ADDR_SURFTYPE_BSS : LSC_ADDR_SURFTYPE_BTI);
      srcs[MEMORY_LOGICAL_BINDING] =
         get_nir_buffer_intrinsic_index(ntb, bld, instr, &no_mask_handle);
      srcs[MEMORY_LOGICAL_ADDRESS] =
         get_nir_src(ntb, instr->src[is_store ? 2 : 1]);

      data_src = is_atomic ? 2 : 0;
      break;
   case nir_intrinsic_load_shared:
   case nir_intrinsic_store_shared:
   case nir_intrinsic_shared_atomic:
   case nir_intrinsic_shared_atomic_swap:
   case nir_intrinsic_load_shared_block_intel:
   case nir_intrinsic_store_shared_block_intel:
   case nir_intrinsic_load_shared_uniform_block_intel: {
      srcs[MEMORY_LOGICAL_MODE] = brw_imm_ud(MEMORY_MODE_SHARED_LOCAL);
      srcs[MEMORY_LOGICAL_BINDING_TYPE] = brw_imm_ud(LSC_ADDR_SURFTYPE_FLAT);

      const nir_src &nir_src = instr->src[is_store ? 1 : 0];

      srcs[MEMORY_LOGICAL_ADDRESS] = nir_src_is_const(nir_src) ?
         brw_imm_ud(nir_intrinsic_base(instr) + nir_src_as_uint(nir_src)) :
         bld.ADD(retype(get_nir_src(ntb, nir_src), BRW_TYPE_UD),
                 brw_imm_ud(nir_intrinsic_base(instr)));

      data_src = is_atomic ? 1 : 0;
      no_mask_handle = true;
      break;
   }
   case nir_intrinsic_load_scratch:
   case nir_intrinsic_store_scratch: {
      srcs[MEMORY_LOGICAL_MODE] = brw_imm_ud(MEMORY_MODE_SCRATCH);

      const nir_src &addr = instr->src[is_store ? 1 : 0];

      if (devinfo->verx10 >= 125) {
         srcs[MEMORY_LOGICAL_BINDING_TYPE] = brw_imm_ud(LSC_ADDR_SURFTYPE_SS);

         const fs_builder ubld = bld.exec_all().group(1, 0);
         brw_reg bind = component(ubld.vgrf(BRW_TYPE_UD), 0);
         ubld.AND(bind, retype(brw_vec1_grf(0, 5), BRW_TYPE_UD),
                        brw_imm_ud(INTEL_MASK(31, 10)));
         if (devinfo->ver >= 20)
            bind = component(ubld.SHR(bind, brw_imm_ud(4)), 0);

         srcs[MEMORY_LOGICAL_BINDING] = bind;
         srcs[MEMORY_LOGICAL_ADDRESS] =
            swizzle_nir_scratch_addr(ntb, bld, addr, false);
      } else {
         unsigned bit_size =
            is_store ? nir_src_bit_size(instr->src[0]) : instr->def.bit_size;
         bool dword_aligned = align >= 4 && bit_size == 32;
         srcs[MEMORY_LOGICAL_BINDING_TYPE] =
            brw_imm_ud(LSC_ADDR_SURFTYPE_FLAT);
         srcs[MEMORY_LOGICAL_ADDRESS] =
            swizzle_nir_scratch_addr(ntb, bld, addr, dword_aligned);
      }

      if (is_store)
         ++s.shader_stats.spill_count;
      else
         ++s.shader_stats.fill_count;

      data_src = 0;
      break;
   }

   case nir_intrinsic_load_global_constant_uniform_block_intel:
      no_mask_handle =
         ntb.uniform_values[instr->src[0].ssa->index].file != BAD_FILE;
      FALLTHROUGH;
   case nir_intrinsic_load_global:
   case nir_intrinsic_load_global_constant:
   case nir_intrinsic_store_global:
   case nir_intrinsic_global_atomic:
   case nir_intrinsic_global_atomic_swap:
   case nir_intrinsic_load_global_block_intel:
   case nir_intrinsic_store_global_block_intel:
      srcs[MEMORY_LOGICAL_MODE] = brw_imm_ud(MEMORY_MODE_UNTYPED);
      srcs[MEMORY_LOGICAL_BINDING_TYPE] = brw_imm_ud(LSC_ADDR_SURFTYPE_FLAT);
      srcs[MEMORY_LOGICAL_ADDRESS] = get_nir_src(ntb, instr->src[is_store ? 1 : 0]);

      data_src = is_atomic ? 1 : 0;
      break;

   default:
      unreachable("unknown memory intrinsic");
   }

   unsigned components = is_store ? instr->src[data_src].ssa->num_components
                                  : instr->def.num_components;
   if (components == 0)
      components = instr->num_components;

   srcs[MEMORY_LOGICAL_COMPONENTS] = brw_imm_ud(components);

   const unsigned nir_bit_size =
      is_store ? instr->src[data_src].ssa->bit_size : instr->def.bit_size;
   enum lsc_data_size data_size = lsc_bits_to_data_size(nir_bit_size);
   uint32_t data_bit_size = lsc_data_size_bytes(data_size) * 8;

   srcs[MEMORY_LOGICAL_DATA_SIZE] = brw_imm_ud(data_size);

   const brw_reg_type data_type =
      brw_type_with_size(BRW_TYPE_UD, data_bit_size);
   const brw_reg_type nir_data_type =
      brw_type_with_size(BRW_TYPE_UD, nir_bit_size);
   assert(data_bit_size >= nir_bit_size);

   if (!is_load) {
      for (unsigned i = 0; i < lsc_op_num_data_values(op); i++) {
         brw_reg nir_src =
            retype(get_nir_src(ntb, instr->src[data_src + i]), nir_data_type);

         if (data_bit_size > nir_bit_size) {
            /* Expand e.g. D16 to D16U32 */
            srcs[MEMORY_LOGICAL_DATA0 + i] = bld.vgrf(data_type, components);
            for (unsigned c = 0; c < components; c++) {
               bld.MOV(offset(srcs[MEMORY_LOGICAL_DATA0 + i], bld, c),
                       offset(nir_src, bld, c));
            }
         } else {
            srcs[MEMORY_LOGICAL_DATA0 + i] = nir_src;
         }
      }
   }

   brw_reg dest, nir_dest;
   if (!is_store) {
      nir_dest = retype(get_nir_def(ntb, instr->def), nir_data_type);
      dest = data_bit_size > nir_bit_size ? bld.vgrf(data_type, components)
                                          : nir_dest;
   }

   enum opcode opcode = is_load ? SHADER_OPCODE_MEMORY_LOAD_LOGICAL :
                        is_store ? SHADER_OPCODE_MEMORY_STORE_LOGICAL :
                        SHADER_OPCODE_MEMORY_ATOMIC_LOGICAL;

   const bool convergent_block_load =
      instr->intrinsic == nir_intrinsic_load_ubo_uniform_block_intel ||
      instr->intrinsic == nir_intrinsic_load_ssbo_uniform_block_intel ||
      instr->intrinsic == nir_intrinsic_load_shared_uniform_block_intel ||
      instr->intrinsic == nir_intrinsic_load_global_constant_uniform_block_intel;
   const bool block = convergent_block_load ||
      instr->intrinsic == nir_intrinsic_load_global_block_intel ||
      instr->intrinsic == nir_intrinsic_load_shared_block_intel ||
      instr->intrinsic == nir_intrinsic_load_ssbo_block_intel ||
      instr->intrinsic == nir_intrinsic_store_global_block_intel ||
      instr->intrinsic == nir_intrinsic_store_shared_block_intel ||
      instr->intrinsic == nir_intrinsic_store_ssbo_block_intel;

   fs_inst *inst;

   if (!block) {
      inst = bld.emit(opcode, dest, srcs, MEMORY_LOGICAL_NUM_SRCS);
      inst->size_written *= components;

      if (dest.file != BAD_FILE && data_bit_size > nir_bit_size) {
         /* Shrink e.g. D16U32 result back to D16 */
         for (unsigned i = 0; i < components; i++) {
            bld.MOV(offset(nir_dest, bld, i),
                    subscript(offset(dest, bld, i), nir_dest.type, 0));
         }
      }
   } else {
      assert(nir_bit_size == 32);

      srcs[MEMORY_LOGICAL_FLAGS] =
         brw_imm_ud(MEMORY_FLAG_TRANSPOSE | srcs[MEMORY_LOGICAL_FLAGS].ud);
      srcs[MEMORY_LOGICAL_ADDRESS] =
         instr->intrinsic == nir_intrinsic_load_global_constant_uniform_block_intel &&
         ntb.uniform_values[instr->src[0].ssa->index].file != BAD_FILE ?
         ntb.uniform_values[instr->src[0].ssa->index] :
         bld.emit_uniformize(srcs[MEMORY_LOGICAL_ADDRESS]);

      const fs_builder ubld = bld.exec_all().group(1, 0);
      unsigned total, done;

      if (convergent_block_load) {
         total = ALIGN(components, REG_SIZE * reg_unit(devinfo) / 4);
         dest = ubld.vgrf(BRW_TYPE_UD, total);
      } else {
         total = components * bld.dispatch_width();
         dest = nir_dest;
      }

      brw_reg src = srcs[MEMORY_LOGICAL_DATA0];

      unsigned block_comps = components;

      for (done = 0; done < total; done += block_comps) {
         block_comps = choose_oword_block_size_dwords(devinfo, total - done);
         const unsigned block_bytes = block_comps * (nir_bit_size / 8);

         srcs[MEMORY_LOGICAL_COMPONENTS] = brw_imm_ud(block_comps);

         brw_reg dst_offset = is_store ? brw_reg() :
            retype(byte_offset(dest, done * 4), BRW_TYPE_UD);
         if (is_store) {
            srcs[MEMORY_LOGICAL_DATA0] =
               retype(byte_offset(src, done * 4), BRW_TYPE_UD);
         }

         inst = ubld.emit(opcode, dst_offset, srcs, MEMORY_LOGICAL_NUM_SRCS);
         inst->has_no_mask_send_params = no_mask_handle;
         if (is_load)
            inst->size_written = block_bytes;

         if (brw_type_size_bits(srcs[MEMORY_LOGICAL_ADDRESS].type) == 64) {
            increment_a64_address(ubld, srcs[MEMORY_LOGICAL_ADDRESS],
                                  block_bytes, no_mask_handle);
         } else {
            srcs[MEMORY_LOGICAL_ADDRESS] =
               ubld.ADD(retype(srcs[MEMORY_LOGICAL_ADDRESS], BRW_TYPE_UD),
                        brw_imm_ud(block_bytes));
         }
      }
      assert(done == total);

      if (convergent_block_load) {
         for (unsigned c = 0; c < components; c++) {
            bld.MOV(retype(offset(nir_dest, bld, c), BRW_TYPE_UD),
                    component(dest, c));
         }
      }
   }
}

static void
fs_nir_emit_texture(nir_to_brw_state &ntb,
                    nir_tex_instr *instr)
{
   const intel_device_info *devinfo = ntb.devinfo;
   const fs_builder &bld = ntb.bld;

   brw_reg srcs[TEX_LOGICAL_NUM_SRCS];

   /* SKL PRMs: Volume 7: 3D-Media-GPGPU:
    *
    *    "The Pixel Null Mask field, when enabled via the Pixel Null Mask
    *     Enable will be incorect for sample_c when applied to a surface with
    *     64-bit per texel format such as R16G16BA16_UNORM. Pixel Null mask
    *     Enable may incorrectly report pixels as referencing a Null surface."
    *
    * We'll take care of this in NIR.
    */
   assert(!instr->is_sparse || srcs[TEX_LOGICAL_SRC_SHADOW_C].file == BAD_FILE);

   srcs[TEX_LOGICAL_SRC_RESIDENCY] = brw_imm_ud(instr->is_sparse);

   int lod_components = 0;

   /* The hardware requires a LOD for buffer textures */
   if (instr->sampler_dim == GLSL_SAMPLER_DIM_BUF)
      srcs[TEX_LOGICAL_SRC_LOD] = brw_imm_d(0);

   ASSERTED bool got_lod = false;
   ASSERTED bool got_bias = false;
   bool pack_lod_bias_and_offset = false;
   uint32_t header_bits = 0;
   for (unsigned i = 0; i < instr->num_srcs; i++) {
      nir_src nir_src = instr->src[i].src;
      brw_reg src = get_nir_src(ntb, nir_src);
      switch (instr->src[i].src_type) {
      case nir_tex_src_bias:
         assert(!got_lod);
         got_bias = true;

         srcs[TEX_LOGICAL_SRC_LOD] =
            retype(get_nir_src_imm(ntb, instr->src[i].src), BRW_TYPE_F);
         break;
      case nir_tex_src_comparator:
         srcs[TEX_LOGICAL_SRC_SHADOW_C] = retype(src, BRW_TYPE_F);
         break;
      case nir_tex_src_coord:
         switch (instr->op) {
         case nir_texop_txf:
         case nir_texop_txf_ms:
         case nir_texop_txf_ms_mcs_intel:
         case nir_texop_samples_identical:
            srcs[TEX_LOGICAL_SRC_COORDINATE] = retype(src, BRW_TYPE_D);
            break;
         default:
            srcs[TEX_LOGICAL_SRC_COORDINATE] = retype(src, BRW_TYPE_F);
            break;
         }
         break;
      case nir_tex_src_ddx:
         srcs[TEX_LOGICAL_SRC_LOD] = retype(src, BRW_TYPE_F);
         lod_components = nir_tex_instr_src_size(instr, i);
         break;
      case nir_tex_src_ddy:
         srcs[TEX_LOGICAL_SRC_LOD2] = retype(src, BRW_TYPE_F);
         break;
      case nir_tex_src_lod:
         assert(!got_bias);
         got_lod = true;

         switch (instr->op) {
         case nir_texop_txs:
            srcs[TEX_LOGICAL_SRC_LOD] =
               retype(get_nir_src_imm(ntb, instr->src[i].src), BRW_TYPE_UD);
            break;
         case nir_texop_txf:
            srcs[TEX_LOGICAL_SRC_LOD] =
               retype(get_nir_src_imm(ntb, instr->src[i].src), BRW_TYPE_D);
            break;
         default:
            srcs[TEX_LOGICAL_SRC_LOD] =
               retype(get_nir_src_imm(ntb, instr->src[i].src), BRW_TYPE_F);
            break;
         }
         break;
      case nir_tex_src_min_lod:
         srcs[TEX_LOGICAL_SRC_MIN_LOD] =
            retype(get_nir_src_imm(ntb, instr->src[i].src), BRW_TYPE_F);
         break;
      case nir_tex_src_ms_index:
         srcs[TEX_LOGICAL_SRC_SAMPLE_INDEX] = retype(src, BRW_TYPE_UD);
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
               retype(src, BRW_TYPE_D);
         }
         break;
      }

      case nir_tex_src_projector:
         unreachable("should be lowered");

      case nir_tex_src_texture_offset: {
         assert(srcs[TEX_LOGICAL_SRC_SURFACE].file == BAD_FILE);
         /* Emit code to evaluate the actual indexing expression */
         if (instr->texture_index == 0 && is_resource_src(nir_src))
            srcs[TEX_LOGICAL_SRC_SURFACE] = get_resource_nir_src(ntb, nir_src);
         if (srcs[TEX_LOGICAL_SRC_SURFACE].file == BAD_FILE) {
            srcs[TEX_LOGICAL_SRC_SURFACE] =
               bld.emit_uniformize(bld.ADD(retype(src, BRW_TYPE_UD),
                                           brw_imm_ud(instr->texture_index)));
         }
         assert(srcs[TEX_LOGICAL_SRC_SURFACE].file != BAD_FILE);
         break;
      }

      case nir_tex_src_sampler_offset: {
         /* Emit code to evaluate the actual indexing expression */
         if (instr->sampler_index == 0 && is_resource_src(nir_src))
            srcs[TEX_LOGICAL_SRC_SAMPLER] = get_resource_nir_src(ntb, nir_src);
         if (srcs[TEX_LOGICAL_SRC_SAMPLER].file == BAD_FILE) {
            srcs[TEX_LOGICAL_SRC_SAMPLER] =
               bld.emit_uniformize(bld.ADD(retype(src, BRW_TYPE_UD),
                                           brw_imm_ud(instr->sampler_index)));
         }
         break;
      }

      case nir_tex_src_texture_handle:
         assert(nir_tex_instr_src_index(instr, nir_tex_src_texture_offset) == -1);
         srcs[TEX_LOGICAL_SRC_SURFACE] = brw_reg();
         if (is_resource_src(nir_src))
            srcs[TEX_LOGICAL_SRC_SURFACE_HANDLE] = get_resource_nir_src(ntb, nir_src);
         if (srcs[TEX_LOGICAL_SRC_SURFACE_HANDLE].file == BAD_FILE)
            srcs[TEX_LOGICAL_SRC_SURFACE_HANDLE] = bld.emit_uniformize(src);
         break;

      case nir_tex_src_sampler_handle:
         assert(nir_tex_instr_src_index(instr, nir_tex_src_sampler_offset) == -1);
         srcs[TEX_LOGICAL_SRC_SAMPLER] = brw_reg();
         if (is_resource_src(nir_src))
            srcs[TEX_LOGICAL_SRC_SAMPLER_HANDLE] = get_resource_nir_src(ntb, nir_src);
         if (srcs[TEX_LOGICAL_SRC_SAMPLER_HANDLE].file == BAD_FILE)
            srcs[TEX_LOGICAL_SRC_SAMPLER_HANDLE] = bld.emit_uniformize(src);
         break;

      case nir_tex_src_ms_mcs_intel:
         assert(instr->op == nir_texop_txf_ms);
         srcs[TEX_LOGICAL_SRC_MCS] = retype(src, BRW_TYPE_D);
         break;

      /* If this parameter is present, we are packing offset U, V and LOD/Bias
       * into a single (32-bit) value.
       */
      case nir_tex_src_backend2:
         assert(instr->op == nir_texop_tg4);
         pack_lod_bias_and_offset = true;
         srcs[TEX_LOGICAL_SRC_LOD] =
            retype(get_nir_src_imm(ntb, instr->src[i].src), BRW_TYPE_F);
         break;

      /* If this parameter is present, we are packing either the explicit LOD
       * or LOD bias and the array index into a single (32-bit) value when
       * 32-bit texture coordinates are used.
       */
      case nir_tex_src_backend1:
         assert(!got_lod && !got_bias);
         got_lod = true;
         assert(instr->op == nir_texop_txl || instr->op == nir_texop_txb);
         srcs[TEX_LOGICAL_SRC_LOD] =
            retype(get_nir_src_imm(ntb, instr->src[i].src), BRW_TYPE_F);
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
      srcs[TEX_LOGICAL_SRC_MCS] =
         emit_mcs_fetch(ntb, srcs[TEX_LOGICAL_SRC_COORDINATE],
                        instr->coord_components,
                        srcs[TEX_LOGICAL_SRC_SURFACE],
                        srcs[TEX_LOGICAL_SRC_SURFACE_HANDLE]);
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
      else
         opcode = SHADER_OPCODE_TXF_CMS_W_LOGICAL;
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
   case nir_texop_tg4: {
      if (srcs[TEX_LOGICAL_SRC_TG4_OFFSET].file != BAD_FILE) {
         opcode = SHADER_OPCODE_TG4_OFFSET_LOGICAL;
      } else {
         opcode = SHADER_OPCODE_TG4_LOGICAL;
         if (devinfo->ver >= 20) {
            /* If SPV_AMD_texture_gather_bias_lod extension is enabled, all
             * texture gather functions (ie. the ones which do not take the
             * extra bias argument and the ones that do) fetch texels from
             * implicit LOD in fragment shader stage. In all other shader
             * stages, base level is used instead.
             */
            if (instr->is_gather_implicit_lod)
               opcode = SHADER_OPCODE_TG4_IMPLICIT_LOD_LOGICAL;

            if (got_bias)
               opcode = SHADER_OPCODE_TG4_BIAS_LOGICAL;

            if (got_lod)
               opcode = SHADER_OPCODE_TG4_EXPLICIT_LOD_LOGICAL;

            if (pack_lod_bias_and_offset) {
               if (got_lod)
                  opcode = SHADER_OPCODE_TG4_OFFSET_LOD_LOGICAL;
               if (got_bias)
                  opcode = SHADER_OPCODE_TG4_OFFSET_BIAS_LOGICAL;
            }
         }
      }
      break;
   }
   case nir_texop_texture_samples:
      opcode = SHADER_OPCODE_SAMPLEINFO_LOGICAL;
      break;
   case nir_texop_samples_identical: {
      brw_reg dst = retype(get_nir_def(ntb, instr->def), BRW_TYPE_D);

      /* If mcs is an immediate value, it means there is no MCS.  In that case
       * just return false.
       */
      if (srcs[TEX_LOGICAL_SRC_MCS].file == IMM) {
         bld.MOV(dst, brw_imm_ud(0u));
      } else {
         brw_reg tmp =
            bld.OR(srcs[TEX_LOGICAL_SRC_MCS],
                   offset(srcs[TEX_LOGICAL_SRC_MCS], bld, 1));
         bld.CMP(dst, tmp, brw_imm_ud(0u), BRW_CONDITIONAL_EQ);
      }
      return;
   }
   default:
      unreachable("unknown texture opcode");
   }

   if (instr->op == nir_texop_tg4) {
      header_bits |= instr->component << 16;
   }

   brw_reg nir_def_reg = get_nir_def(ntb, instr->def);

   const unsigned dest_size = nir_tex_instr_dest_size(instr);
   unsigned dest_comp;
   if (instr->op != nir_texop_tg4 && instr->op != nir_texop_query_levels) {
      unsigned write_mask = nir_def_components_read(&instr->def);
      assert(write_mask != 0); /* dead code should have been eliminated */

      dest_comp = util_last_bit(write_mask) - instr->is_sparse;
   } else {
      dest_comp = 4;
   }

   /* Compute the number of physical registers needed to hold a single
    * component and round it up to a physical register count.
    */
   brw_reg_type dst_type = brw_type_for_nir_type(devinfo, instr->dest_type);
   const unsigned grf_size = reg_unit(devinfo) * REG_SIZE;
   const unsigned per_component_regs =
      DIV_ROUND_UP(brw_type_size_bytes(dst_type) * bld.dispatch_width(),
                   grf_size);
   const unsigned total_regs =
      dest_comp * per_component_regs + instr->is_sparse;
   /* Allocate enough space for the components + one physical register for the
    * residency data.
    */
   brw_reg dst = brw_vgrf(
      bld.shader->alloc.allocate(total_regs * reg_unit(devinfo)),
      dst_type);

   fs_inst *inst = bld.emit(opcode, dst, srcs, ARRAY_SIZE(srcs));
   inst->offset = header_bits;
   inst->size_written = total_regs * grf_size;

   if (srcs[TEX_LOGICAL_SRC_SHADOW_C].file != BAD_FILE)
      inst->shadow_compare = true;

   /* Wa_14012688258:
    *
    * Don't trim zeros at the end of payload for sample operations
    * in cube and cube arrays.
    */
   if (instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE &&
       intel_needs_workaround(devinfo, 14012688258)) {

      /* Compiler should send U,V,R parameters even if V,R are 0. */
      if (srcs[TEX_LOGICAL_SRC_COORDINATE].file != BAD_FILE)
         assert(instr->coord_components >= 3u);

      /* See opt_zero_samples(). */
      inst->keep_payload_trailing_zeros = true;
   }

   /* With half-floats returns, the stride into a GRF allocation for each
    * component might be different than where the sampler is storing each
    * component. For example in SIMD8 on DG2 the layout of the data returned
    * by the sampler is as follow for 2 components load:
    *
    *           _______________________________________________________________
    *   g0 : |           unused              |hf7|hf6|hf5|hf4|hf3|hf2|hf1|hf0|
    *   g1 : |           unused              |hf7|hf6|hf5|hf4|hf3|hf2|hf1|hf0|
    *
    * The same issue also happens in SIMD16 on Xe2 because the physical
    * register size has doubled but we're still loading data only on half the
    * register.
    *
    * In those cases we need the special remapping case below.
    */
   const bool non_aligned_component_stride =
      (brw_type_size_bytes(dst_type) * bld.dispatch_width()) % grf_size != 0;
   if (instr->op != nir_texop_query_levels && !instr->is_sparse &&
       !non_aligned_component_stride) {
      /* In most cases we can write directly to the result. */
      inst->dst = nir_def_reg;
   } else {
      /* In other cases, we have to reorganize the sampler message's results
       * a bit to match the NIR intrinsic's expectations.
       */
      brw_reg nir_dest[5];
      for (unsigned i = 0; i < dest_comp; i++)
         nir_dest[i] = byte_offset(dst, i * per_component_regs * grf_size);

      for (unsigned i = dest_comp; i < dest_size; i++)
         nir_dest[i].type = dst.type;

      if (instr->op == nir_texop_query_levels) {
         /* # levels is in .w */
         if (devinfo->ver == 9) {
            /**
             * Wa_1940217:
             *
             * When a surface of type SURFTYPE_NULL is accessed by resinfo, the
             * MIPCount returned is undefined instead of 0.
             */
            fs_inst *mov = bld.MOV(bld.null_reg_d(), dst);
            mov->conditional_mod = BRW_CONDITIONAL_NZ;
            nir_dest[0] = bld.vgrf(BRW_TYPE_D);
            fs_inst *sel =
               bld.SEL(nir_dest[0], offset(dst, bld, 3), brw_imm_d(0));
            sel->predicate = BRW_PREDICATE_NORMAL;
         } else {
            nir_dest[0] = offset(dst, bld, 3);
         }
      }

      /* The residency bits are only in the first component. */
      if (instr->is_sparse) {
         nir_dest[dest_size - 1] =
            component(offset(dst, bld, dest_size - 1), 0);
      }

      bld.LOAD_PAYLOAD(nir_def_reg, nir_dest, dest_size, 0);
   }
}

static void
fs_nir_emit_jump(nir_to_brw_state &ntb, nir_jump_instr *instr)
{
   switch (instr->type) {
   case nir_jump_break:
      ntb.bld.emit(BRW_OPCODE_BREAK);
      break;
   case nir_jump_continue:
      ntb.bld.emit(BRW_OPCODE_CONTINUE);
      break;
   case nir_jump_halt:
      ntb.bld.emit(BRW_OPCODE_HALT);
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
                   const brw_reg &dst,
                   const brw_reg &src,
                   uint32_t first_component,
                   uint32_t components)
{
   if (brw_type_size_bytes(src.type) == brw_type_size_bytes(dst.type)) {
      assert(!regions_overlap(dst,
         brw_type_size_bytes(dst.type) * bld.dispatch_width() * components,
         offset(src, bld, first_component),
         brw_type_size_bytes(src.type) * bld.dispatch_width() * components));
      for (unsigned i = 0; i < components; i++) {
         bld.MOV(retype(offset(dst, bld, i), src.type),
                 offset(src, bld, i + first_component));
      }
   } else if (brw_type_size_bytes(src.type) < brw_type_size_bytes(dst.type)) {
      /* Source is shuffled into destination */
      unsigned size_ratio = brw_type_size_bytes(dst.type) / brw_type_size_bytes(src.type);
      assert(!regions_overlap(dst,
         brw_type_size_bytes(dst.type) * bld.dispatch_width() *
         DIV_ROUND_UP(components, size_ratio),
         offset(src, bld, first_component),
         brw_type_size_bytes(src.type) * bld.dispatch_width() * components));

      brw_reg_type shuffle_type =
         brw_type_with_size(BRW_TYPE_D, brw_type_size_bits(src.type));
      for (unsigned i = 0; i < components; i++) {
         brw_reg shuffle_component_i =
            subscript(offset(dst, bld, i / size_ratio),
                      shuffle_type, i % size_ratio);
         bld.MOV(shuffle_component_i,
                 retype(offset(src, bld, i + first_component), shuffle_type));
      }
   } else {
      /* Source is unshuffled into destination */
      unsigned size_ratio = brw_type_size_bytes(src.type) / brw_type_size_bytes(dst.type);
      assert(!regions_overlap(dst,
         brw_type_size_bytes(dst.type) * bld.dispatch_width() * components,
         offset(src, bld, first_component / size_ratio),
         brw_type_size_bytes(src.type) * bld.dispatch_width() *
         DIV_ROUND_UP(components + (first_component % size_ratio),
                      size_ratio)));

      brw_reg_type shuffle_type =
         brw_type_with_size(BRW_TYPE_D, brw_type_size_bits(dst.type));
      for (unsigned i = 0; i < components; i++) {
         brw_reg shuffle_component_i =
            subscript(offset(src, bld, (first_component + i) / size_ratio),
                      shuffle_type, (first_component + i) % size_ratio);
         bld.MOV(retype(offset(dst, bld, i), shuffle_type),
                 shuffle_component_i);
      }
   }
}

void
shuffle_from_32bit_read(const fs_builder &bld,
                        const brw_reg &dst,
                        const brw_reg &src,
                        uint32_t first_component,
                        uint32_t components)
{
   assert(brw_type_size_bytes(src.type) == 4);

   /* This function takes components in units of the destination type while
    * shuffle_src_to_dst takes components in units of the smallest type
    */
   if (brw_type_size_bytes(dst.type) > 4) {
      assert(brw_type_size_bytes(dst.type) == 8);
      first_component *= 2;
      components *= 2;
   }

   shuffle_src_to_dst(bld, dst, src, first_component, components);
}

static void
fs_nir_emit_instr(nir_to_brw_state &ntb, nir_instr *instr)
{
#ifndef NDEBUG
   if (unlikely(ntb.annotate)) {
      /* Use shader mem_ctx since annotations outlive the NIR conversion. */
      ntb.bld = ntb.bld.annotate(nir_instr_as_str(instr, ntb.s.mem_ctx));
   }
#endif

   switch (instr->type) {
   case nir_instr_type_alu:
      fs_nir_emit_alu(ntb, nir_instr_as_alu(instr), true);
      break;

   case nir_instr_type_deref:
      unreachable("All derefs should've been lowered");
      break;

   case nir_instr_type_intrinsic:
      switch (ntb.s.stage) {
      case MESA_SHADER_VERTEX:
         fs_nir_emit_vs_intrinsic(ntb, nir_instr_as_intrinsic(instr));
         break;
      case MESA_SHADER_TESS_CTRL:
         fs_nir_emit_tcs_intrinsic(ntb, nir_instr_as_intrinsic(instr));
         break;
      case MESA_SHADER_TESS_EVAL:
         fs_nir_emit_tes_intrinsic(ntb, nir_instr_as_intrinsic(instr));
         break;
      case MESA_SHADER_GEOMETRY:
         fs_nir_emit_gs_intrinsic(ntb, nir_instr_as_intrinsic(instr));
         break;
      case MESA_SHADER_FRAGMENT:
         fs_nir_emit_fs_intrinsic(ntb, nir_instr_as_intrinsic(instr));
         break;
      case MESA_SHADER_COMPUTE:
      case MESA_SHADER_KERNEL:
         fs_nir_emit_cs_intrinsic(ntb, nir_instr_as_intrinsic(instr));
         break;
      case MESA_SHADER_RAYGEN:
      case MESA_SHADER_ANY_HIT:
      case MESA_SHADER_CLOSEST_HIT:
      case MESA_SHADER_MISS:
      case MESA_SHADER_INTERSECTION:
      case MESA_SHADER_CALLABLE:
         fs_nir_emit_bs_intrinsic(ntb, nir_instr_as_intrinsic(instr));
         break;
      case MESA_SHADER_TASK:
         fs_nir_emit_task_intrinsic(ntb, nir_instr_as_intrinsic(instr));
         break;
      case MESA_SHADER_MESH:
         fs_nir_emit_mesh_intrinsic(ntb, nir_instr_as_intrinsic(instr));
         break;
      default:
         unreachable("unsupported shader stage");
      }
      break;

   case nir_instr_type_tex:
      fs_nir_emit_texture(ntb, nir_instr_as_tex(instr));
      break;

   case nir_instr_type_load_const:
      fs_nir_emit_load_const(ntb, nir_instr_as_load_const(instr));
      break;

   case nir_instr_type_undef:
      /* We create a new VGRF for undefs on every use (by handling
       * them in get_nir_src()), rather than for each definition.
       * This helps register coalescing eliminate MOVs from undef.
       */
      break;

   case nir_instr_type_jump:
      fs_nir_emit_jump(ntb, nir_instr_as_jump(instr));
      break;

   default:
      unreachable("unknown instruction type");
   }
}

static unsigned
brw_rnd_mode_from_nir(unsigned mode, unsigned *mask)
{
   unsigned brw_mode = 0;
   *mask = 0;

   if ((FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP16 |
        FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP32 |
        FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP64) &
       mode) {
      brw_mode |= BRW_RND_MODE_RTZ << BRW_CR0_RND_MODE_SHIFT;
      *mask |= BRW_CR0_RND_MODE_MASK;
   }
   if ((FLOAT_CONTROLS_ROUNDING_MODE_RTE_FP16 |
        FLOAT_CONTROLS_ROUNDING_MODE_RTE_FP32 |
        FLOAT_CONTROLS_ROUNDING_MODE_RTE_FP64) &
       mode) {
      brw_mode |= BRW_RND_MODE_RTNE << BRW_CR0_RND_MODE_SHIFT;
      *mask |= BRW_CR0_RND_MODE_MASK;
   }
   if (mode & FLOAT_CONTROLS_DENORM_PRESERVE_FP16) {
      brw_mode |= BRW_CR0_FP16_DENORM_PRESERVE;
      *mask |= BRW_CR0_FP16_DENORM_PRESERVE;
   }
   if (mode & FLOAT_CONTROLS_DENORM_PRESERVE_FP32) {
      brw_mode |= BRW_CR0_FP32_DENORM_PRESERVE;
      *mask |= BRW_CR0_FP32_DENORM_PRESERVE;
   }
   if (mode & FLOAT_CONTROLS_DENORM_PRESERVE_FP64) {
      brw_mode |= BRW_CR0_FP64_DENORM_PRESERVE;
      *mask |= BRW_CR0_FP64_DENORM_PRESERVE;
   }
   if (mode & FLOAT_CONTROLS_DENORM_FLUSH_TO_ZERO_FP16)
      *mask |= BRW_CR0_FP16_DENORM_PRESERVE;
   if (mode & FLOAT_CONTROLS_DENORM_FLUSH_TO_ZERO_FP32)
      *mask |= BRW_CR0_FP32_DENORM_PRESERVE;
   if (mode & FLOAT_CONTROLS_DENORM_FLUSH_TO_ZERO_FP64)
      *mask |= BRW_CR0_FP64_DENORM_PRESERVE;
   if (mode == FLOAT_CONTROLS_DEFAULT_FLOAT_CONTROL_MODE)
      *mask |= BRW_CR0_FP_MODE_MASK;

   if (*mask != 0)
      assert((*mask & brw_mode) == brw_mode);

   return brw_mode;
}

static void
emit_shader_float_controls_execution_mode(nir_to_brw_state &ntb)
{
   const fs_builder &bld = ntb.bld;
   fs_visitor &s = ntb.s;

   unsigned execution_mode = s.nir->info.float_controls_execution_mode;
   if (execution_mode == FLOAT_CONTROLS_DEFAULT_FLOAT_CONTROL_MODE)
      return;

   fs_builder ubld = bld.exec_all().group(1, 0);
   fs_builder abld = ubld.annotate("shader floats control execution mode");
   unsigned mask, mode = brw_rnd_mode_from_nir(execution_mode, &mask);

   if (mask == 0)
      return;

   abld.emit(SHADER_OPCODE_FLOAT_CONTROL_MODE, bld.null_reg_ud(),
             brw_imm_d(mode), brw_imm_d(mask));
}

/**
 * Test the dispatch mask packing assumptions of
 * brw_stage_has_packed_dispatch().  Call this from e.g. the top of
 * nir_to_brw() to cause a GPU hang if any shader invocation is
 * executed with an unexpected dispatch mask.
 */
static UNUSED void
brw_fs_test_dispatch_packing(const fs_builder &bld)
{
   const fs_visitor *shader = bld.shader;
   const gl_shader_stage stage = shader->stage;
   const bool uses_vmask =
      stage == MESA_SHADER_FRAGMENT &&
      brw_wm_prog_data(shader->prog_data)->uses_vmask;

   if (brw_stage_has_packed_dispatch(shader->devinfo, stage,
                                     shader->max_polygons,
                                     shader->prog_data)) {
      const fs_builder ubld = bld.exec_all().group(1, 0);
      const brw_reg tmp = component(bld.vgrf(BRW_TYPE_UD), 0);
      const brw_reg mask = uses_vmask ? brw_vmask_reg() : brw_dmask_reg();

      ubld.ADD(tmp, mask, brw_imm_ud(1));
      ubld.AND(tmp, mask, tmp);

      /* This will loop forever if the dispatch mask doesn't have the expected
       * form '2^n-1', in which case tmp will be non-zero.
       */
      bld.emit(BRW_OPCODE_DO);
      bld.CMP(bld.null_reg_ud(), tmp, brw_imm_ud(0), BRW_CONDITIONAL_NZ);
      set_predicate(BRW_PREDICATE_NORMAL, bld.emit(BRW_OPCODE_WHILE));
   }
}

void
nir_to_brw(fs_visitor *s)
{
   nir_to_brw_state ntb = {
      .s       = *s,
      .nir     = s->nir,
      .devinfo = s->devinfo,
      .mem_ctx = ralloc_context(NULL),
      .bld     = fs_builder(s).at_end(),
   };

   if (INTEL_DEBUG(DEBUG_ANNOTATION))
      ntb.annotate = true;

   if (ENABLE_FS_TEST_DISPATCH_PACKING)
      brw_fs_test_dispatch_packing(ntb.bld);

   for (unsigned i = 0; i < s->nir->printf_info_count; i++) {
      brw_stage_prog_data_add_printf(s->prog_data,
                                     s->mem_ctx,
                                     &s->nir->printf_info[i]);
   }

   emit_shader_float_controls_execution_mode(ntb);

   /* emit the arrays used for inputs and outputs - load/store intrinsics will
    * be converted to reads/writes of these arrays
    */
   fs_nir_setup_outputs(ntb);
   fs_nir_setup_uniforms(ntb.s);
   fs_nir_emit_system_values(ntb);
   ntb.s.last_scratch = ALIGN(ntb.nir->scratch_size, 4) * ntb.s.dispatch_width;

   fs_nir_emit_impl(ntb, nir_shader_get_entrypoint((nir_shader *)ntb.nir));

   ntb.bld.emit(SHADER_OPCODE_HALT_TARGET);

   ralloc_free(ntb.mem_ctx);

   brw_shader_phase_update(*s, BRW_SHADER_PHASE_AFTER_NIR);
}
