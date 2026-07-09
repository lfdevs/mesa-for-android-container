/*
 * Copyright © 2014-2015 Broadcom
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include "nir_to_rc.h"
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_deref.h"
#include "compiler/nir/nir_legacy.h"
#include "compiler/radeon_code.h"
#include "compiler/radeon_compiler.h"
#include "compiler/radeon_program.h"
#include "compiler/radeon_program_constants.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "util/compiler.h"
#include "util/u_debug.h"
#include "util/u_dynarray.h"
#include "util/u_math.h"
#include "util/u_memory.h"
#include "r300_nir.h"
#include "r300_screen.h"

struct ntr_immediate {
   float values[4];
};

struct ntr_state_constant {
   unsigned rc_state;
   unsigned sampler;
};

struct ntr_compile {
   nir_shader *s;
   struct r300_shader_semantics *semantics;

   /* Options */
   bool lower_fabs;

   /* r300's RC backend only tracks a single ADDR[0].x; logical address
    * indexing collapses to that one register. */
   struct rc_dst_register addr_reg;

   /* if condition set up at the end of a block, for ntr_emit_if(). */
   struct rc_src_register if_cond;

   /* Temps for our NIR SSA and register values. */
   struct rc_dst_register *reg_temp;
   struct rc_src_register *ssa_temp;

   unsigned num_temps;

   /* Map from NIR driver_location to RC input register. */
   struct rc_src_register *input_index_map;

   /* RC-side state for direct emission. */
   struct radeon_compiler *compiler;
   /* one struct ntr_immediate per NIR load_const */
   struct util_dynarray immediates;
   /* one struct ntr_state_constant per private state load */
   struct util_dynarray state_constants;
   /* UBO size in vec4s (0 if no UBO) */
   unsigned ubo_size;
   /* offset for RC_CONSTANT_STATE indices in the compiler's constants table */
   unsigned state_offset;
   /* offset for IMMEDIATE indices in the compiler's constants table */
   unsigned immediate_offset;

   /* FS output tracking. */
   int fs_output_color_index[4];
   int fs_output_depth_index;
   int fs_output_index[FRAG_RESULT_MAX];
   unsigned num_outputs;
};

struct ntr_alu_dst {
   struct rc_dst_register reg;
   rc_saturate_mode saturate;
};

static unsigned ntr_rc_register_index(struct ntr_compile *c, int index);

static struct rc_dst_register
ntr_temp(struct ntr_compile *c)
{
   return (struct rc_dst_register) {
      .File = RC_FILE_TEMPORARY,
      .Index = ntr_rc_register_index(c, c->num_temps++),
      .WriteMask = RC_MASK_XYZW,
   };
}

static void ntr_emit_cf_list(struct ntr_compile *c, struct exec_list *list);

static unsigned
ntr_rc_register_index(struct ntr_compile *c, int index)
{
   /* Negative offsets to relative addressing should have been lowered in NIR. */
   assert(index >= 0);
   if (index >= RC_REGISTER_MAX_INDEX)
      rc_error(c->compiler, "r300: Register index too high.\n");
   return index;
}

static struct rc_dst_register
ntr_dst_register(struct ntr_compile *c, rc_register_file file, int index)
{
   return (struct rc_dst_register) {
      .File = file,
      .Index = ntr_rc_register_index(c, index),
      .WriteMask = RC_MASK_XYZW,
   };
}

static struct rc_dst_register
ntr_writemask(struct rc_dst_register dst, unsigned writemask)
{
   dst.WriteMask &= writemask;
   return dst;
}

static struct ntr_alu_dst
ntr_alu_dst(struct rc_dst_register reg, rc_saturate_mode saturate)
{
   return (struct ntr_alu_dst) {
      .reg = reg,
      .saturate = saturate,
   };
}

static struct ntr_alu_dst
ntr_alu_dst_saturate(struct ntr_alu_dst dst)
{
   dst.saturate = RC_SATURATE_ZERO_ONE;
   return dst;
}

static void
ntr_set_dst_index(struct ntr_compile *c, struct rc_dst_register *dst, int index)
{
   dst->Index = ntr_rc_register_index(c, index);
}

static struct rc_src_register
ntr_src_register(struct ntr_compile *c, rc_register_file file, int index)
{
   return (struct rc_src_register) {
      .File = file,
      .Index = ntr_rc_register_index(c, index),
      .Swizzle = RC_SWIZZLE_XYZW,
      .Negate = RC_MASK_NONE,
   };
}

static void
ntr_set_src_index(struct ntr_compile *c, struct rc_src_register *src, int index)
{
   src->Index = ntr_rc_register_index(c, index);
}

static struct rc_src_register
ntr_src_from_dst(struct rc_dst_register dst)
{
   return (struct rc_src_register) {
      .File = dst.File,
      .Index = dst.Index,
      .Swizzle = RC_SWIZZLE_XYZW,
      .Negate = RC_MASK_NONE,
   };
}

static struct rc_src_register
ntr_swizzle(struct rc_src_register src, int x, int y, int z, int w)
{
   int swizzle[4] = {x, y, z, w};
   unsigned old_swizzle = src.Swizzle;
   unsigned old_negate = src.Negate;

   src.Swizzle = 0;
   src.Negate = RC_MASK_NONE;

   for (unsigned i = 0; i < 4; i++) {
      assert(swizzle[i] < 4);
      SET_SWZ(src.Swizzle, i, GET_SWZ(old_swizzle, swizzle[i]));
      if (old_negate & (1 << swizzle[i]))
         src.Negate |= 1 << i;
   }

   return src;
}

static struct rc_src_register
ntr_scalar(struct rc_src_register src, int x)
{
   return ntr_swizzle(src, x, x, x, x);
}

static struct rc_src_register
ntr_abs(struct rc_src_register src)
{
   src.Abs = true;
   src.Negate = RC_MASK_NONE;
   return src;
}

static struct rc_src_register
ntr_negate(struct rc_src_register src)
{
   src.Negate ^= RC_MASK_XYZW;
   return src;
}

static struct rc_src_register
ntr_src_indirect(struct rc_src_register src)
{
   src.RelAddr = true;
   return src;
}

static unsigned
ntr_add_state_constant(struct ntr_compile *c, unsigned rc_state, unsigned sampler)
{
   assert(rc_state <= RC_STATE_R300_VIEWPORT_OFFSET);

   unsigned index = 0;
   util_dynarray_foreach (&c->state_constants, struct ntr_state_constant, state) {
      if (state->rc_state == rc_state && state->sampler == sampler)
         return index;
      index++;
   }

   struct ntr_state_constant *state =
      util_dynarray_grow(&c->state_constants, struct ntr_state_constant, 1);
   state->rc_state = rc_state;
   state->sampler = sampler;

   return index;
}

static nir_def *
ntr_load_state_constant(struct ntr_compile *c, nir_builder *b,
                        unsigned rc_state, unsigned sampler,
                        unsigned num_components)
{
   unsigned index = ntr_add_state_constant(c, rc_state, sampler);
   /* This is a private marker consumed by ntr_emit_load_uniform, not a user
    * uniform. Real uniforms were already lowered to UBOs before r300 inserts
    * these state loads, so there should be no other load_uniform intrinsics
    * and base here encodes the state constant table index.
    */
   return nir_load_uniform(b, num_components, 32, nir_imm_int(b, 0),
                           .base = index,
                           .range = num_components,
                           .dest_type = nir_type_float32);
}

static void
ntr_rc_tex_sampler(struct ntr_compile *c, struct rc_sub_instruction *inst,
                   unsigned sampler_index, bool reladdr)
{
   if (reladdr)
      rc_error(c->compiler,
               "r300: Relative addressing of sampler operands is unsupported.\n");
   inst->TexSrcUnit = ntr_rc_register_index(c, sampler_index);
}

static struct rc_sub_instruction *
rc_sub_instruction(struct ntr_compile *c, rc_opcode opcode,
                   const struct rc_dst_register *dst,
                   rc_saturate_mode saturate,
                   const struct rc_src_register *src0,
                   const struct rc_src_register *src1,
                   const struct rc_src_register *src2)
{
   const struct rc_opcode_info *opcode_info = rc_get_opcode_info(opcode);
   assert(opcode_info->NumSrcRegs <= 3);
   assert(opcode_info->HasDstReg == (dst != NULL));
   assert(opcode_info->NumSrcRegs >= (src2 ? 3 : src1 ? 2 : src0 ? 1 : 0));

   struct rc_instruction *rc_inst =
      rc_insert_new_instruction(c->compiler, c->compiler->Program.Instructions.Prev);
   struct rc_sub_instruction *inst = &rc_inst->U.I;

   inst->Opcode = opcode;
   if (dst) {
      inst->DstReg = *dst;
      inst->SaturateMode = saturate;
   }
   if (src0)
      inst->SrcReg[0] = *src0;
   if (src1)
      inst->SrcReg[1] = *src1;
   if (src2)
      inst->SrcReg[2] = *src2;

   return inst;
}

static struct rc_sub_instruction *
ntr_emit_alu_op(struct ntr_compile *c, rc_opcode opcode,
                struct ntr_alu_dst dst,
                const struct rc_src_register *src0,
                const struct rc_src_register *src1,
                const struct rc_src_register *src2)
{
   return rc_sub_instruction(c, opcode, &dst.reg, dst.saturate,
                             src0, src1, src2);
}

static inline void
ntr_emit_alu_op1(struct ntr_compile *c, rc_opcode opcode,
                 struct ntr_alu_dst dst, struct rc_src_register src0)
{
   ntr_emit_alu_op(c, opcode, dst, &src0, NULL, NULL);
}

static inline void
ntr_emit_alu_op2(struct ntr_compile *c, rc_opcode opcode,
                 struct ntr_alu_dst dst, struct rc_src_register src0,
                 struct rc_src_register src1)
{
   ntr_emit_alu_op(c, opcode, dst, &src0, &src1, NULL);
}

static inline void
ntr_emit_alu_op3(struct ntr_compile *c, rc_opcode opcode,
                 struct ntr_alu_dst dst, struct rc_src_register src0,
                 struct rc_src_register src1, struct rc_src_register src2)
{
   ntr_emit_alu_op(c, opcode, dst, &src0, &src1, &src2);
}

#define NTR_OP00(name, op)                                                          \
   static inline void ntr_##name(struct ntr_compile *c)                             \
   {                                                                                \
      rc_sub_instruction(c, op, NULL, RC_SATURATE_NONE, NULL, NULL, NULL);           \
   }

#define NTR_OP01(name, op)                                                          \
   static inline void ntr_##name(struct ntr_compile *c, struct rc_src_register src0)\
   {                                                                                \
      rc_sub_instruction(c, op, NULL, RC_SATURATE_NONE, &src0, NULL, NULL);          \
   }

#define NTR_OP11(name, op)                                                          \
   static inline void ntr_##name(struct ntr_compile *c, struct rc_dst_register dst, \
                                 struct rc_src_register src0)                       \
   {                                                                                \
      rc_sub_instruction(c, op, &dst, RC_SATURATE_NONE, &src0, NULL, NULL);          \
   }

NTR_OP00(KILL, RC_OPCODE_KILP)
NTR_OP00(BGNLOOP, RC_OPCODE_BGNLOOP)
NTR_OP00(BRK, RC_OPCODE_BRK)
NTR_OP00(CONT, RC_OPCODE_CONT)
NTR_OP00(ELSE, RC_OPCODE_ELSE)
NTR_OP00(ENDIF, RC_OPCODE_ENDIF)
NTR_OP00(ENDLOOP, RC_OPCODE_ENDLOOP)

NTR_OP01(KILL_IF, RC_OPCODE_KIL)
NTR_OP01(IF, RC_OPCODE_IF)

NTR_OP11(MOV, RC_OPCODE_MOV)
NTR_OP11(ARL, RC_OPCODE_ARL)

/**
 * Interprets a nir_load_const used as a NIR src as a uint.
 *
 * For non-native-integers drivers, nir_load_const_instrs used by an integer ALU
 * instruction (or in a phi-web used by an integer ALU instruction) were
 * converted to floats and the ALU instruction swapped to the float equivalent.
 * However, this means that integer load_consts used by intrinsics (which don't
 * normally get that conversion) may have been reformatted to be floats.  Given
 * that all of our intrinsic nir_src_as_uint() calls are expected to be small,
 * we can just look and see if they look like floats and convert them back to
 * ints.
 */
static uint32_t
ntr_src_as_uint(struct ntr_compile *c, nir_src src)
{
   uint32_t val = nir_src_as_uint(src);
   if (val >= fui(1.0))
      val = (uint32_t)uif(val);
   return val;
}

static void
ntr_read_input_output(struct ntr_compile *c, gl_varying_slot location, unsigned base)
{
   if (base >= c->semantics->num_total)
      c->semantics->num_total = base + 1;

   switch (location) {
   case VARYING_SLOT_POS:
      if (c->s->info.stage == MESA_SHADER_VERTEX)
         c->semantics->pos = base;
      else
         c->semantics->wpos = base;
      break;
   case VARYING_SLOT_PSIZ:
      c->semantics->psize = base;
      break;
   case VARYING_SLOT_COL0:
      c->semantics->color[0] = base;
      break;
   case VARYING_SLOT_COL1:
      c->semantics->color[1] = base;
      break;
   case VARYING_SLOT_BFC0:
      c->semantics->bcolor[0] = base;
      break;
   case VARYING_SLOT_BFC1:
      c->semantics->bcolor[1] = base;
      break;
   case VARYING_SLOT_FOGC:
      c->semantics->fog = base;
      break;
   case VARYING_SLOT_FACE:
      assert(c->s->info.stage == MESA_SHADER_FRAGMENT);
      c->semantics->face = base;
      break;
   case VARYING_SLOT_EDGE:
      assert(c->s->info.stage == MESA_SHADER_VERTEX);
      fprintf(stderr, "r300 VP: cannot handle edgeflag output.\n");
      break;
   default:
      if (location >= VARYING_SLOT_VAR0 && location <= VARYING_SLOT_VAR31) {
         unsigned index = location - VARYING_SLOT_VAR0;
         if (c->semantics->generic[index] == ATTR_UNUSED)
            c->semantics->num_generic++;
         c->semantics->generic[index] = base;
      } else {
         printf("Unhandled varying slot: %u\n", location);
         UNREACHABLE("Unhandled varying slot");
      }
      break;
   }
}

static unsigned
ntr_fs_output_index(struct ntr_compile *c, gl_frag_result location,
                    unsigned dual_source_blend_index)
{
   int color_index = mesa_frag_result_get_color_index(location);
   if (dual_source_blend_index) {
      assert(location == FRAG_RESULT_COLOR || location == FRAG_RESULT_DATA0);
      color_index += dual_source_blend_index;
   }

   int *output_index;
   if (color_index >= 0 && color_index < ARRAY_SIZE(c->fs_output_color_index)) {
      output_index = &c->fs_output_color_index[color_index];
   } else if (location == FRAG_RESULT_DEPTH) {
      output_index = &c->fs_output_depth_index;
   } else {
      assert(location < FRAG_RESULT_MAX);
      output_index = &c->fs_output_index[location];
   }

   if (*output_index < 0)
      *output_index = c->num_outputs++;

   return *output_index;
}

static struct rc_dst_register
ntr_output_decl(struct ntr_compile *c, nir_intrinsic_instr *instr, uint32_t *frac)
{
   nir_io_semantics semantics = nir_intrinsic_io_semantics(instr);
   int base = nir_intrinsic_base(instr);
   *frac = nir_intrinsic_component(instr);

   struct rc_dst_register out;
   if (c->s->info.stage == MESA_SHADER_FRAGMENT) {
      switch (semantics.location) {
      case FRAG_RESULT_DEPTH:
         *frac = 3; /* depth is written to the .w channel in the backend */
         break;
      case FRAG_RESULT_STENCIL:
         *frac = 1;
         break;
      default:
         break;
      }

      out = ntr_dst_register(c, RC_FILE_OUTPUT,
                             ntr_fs_output_index(c, semantics.location,
                                                 semantics.dual_source_blend_index));
   } else {
      ntr_read_input_output(c, semantics.location, base);

      out = ntr_dst_register(c, RC_FILE_OUTPUT, base);
      c->num_outputs = MAX2(c->num_outputs, base + semantics.num_slots);
   }

   unsigned write_mask;
   if (nir_intrinsic_has_write_mask(instr))
      write_mask = nir_intrinsic_write_mask(instr);
   else
      write_mask = ((1 << instr->num_components) - 1) << *frac;

   write_mask = write_mask << *frac;
   return ntr_writemask(out, write_mask);
}

static bool
ntr_try_store_in_tgsi_output_with_use(struct ntr_compile *c, struct rc_dst_register *dst, nir_src *src)
{
   if (nir_src_is_if(src))
      return false;

   if (nir_src_use_instr(src)->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(nir_src_use_instr(src));
   if (intr->intrinsic != nir_intrinsic_store_output || !nir_src_is_const(intr->src[1])) {
      return false;
   }

   uint32_t frac;
   *dst = ntr_output_decl(c, intr, &frac);
   ntr_set_dst_index(c, dst, dst->Index + ntr_src_as_uint(c, intr->src[1]));

   return frac == 0;
}

/* If this reg is used only for storing an output, then in the simple
 * cases we can write directly to the TGSI output instead of having
 * store_output emit its own MOV.
 */
static bool
ntr_try_store_reg_in_tgsi_output(struct ntr_compile *c, struct rc_dst_register *dst,
                                 nir_intrinsic_instr *reg_decl)
{
   assert(reg_decl->intrinsic == nir_intrinsic_decl_reg);

   /* Look for a single use for try_store_in_tgsi_output */
   nir_src *use = NULL;
   nir_foreach_reg_load (src, reg_decl) {
      nir_intrinsic_instr *load = nir_instr_as_intrinsic(nir_src_use_instr(src));
      nir_foreach_use_including_if (load_use, &load->def) {
         /* We can only have one use */
         if (use != NULL)
            return false;

         use = load_use;
      }
   }

   if (use == NULL)
      return false;

   return ntr_try_store_in_tgsi_output_with_use(c, dst, use);
}

/* If this SSA def is used only for storing an output, then in the simple
 * cases we can write directly to the TGSI output instead of having
 * store_output emit its own MOV.
 */
static bool
ntr_try_store_ssa_in_tgsi_output(struct ntr_compile *c, struct rc_dst_register *dst, nir_def *def)
{
   if (!list_is_singular(&def->uses))
      return false;

   nir_foreach_use_including_if (use, def) {
      return ntr_try_store_in_tgsi_output_with_use(c, dst, use);
   }
   UNREACHABLE("We have one use");
}

static void
ntr_setup_inputs(struct ntr_compile *c)
{
   if (c->s->info.stage != MESA_SHADER_FRAGMENT)
      return;

   unsigned num_inputs = 0;
   nir_foreach_shader_in_variable (var, c->s) {
      const struct glsl_type *type = var->type;
      unsigned array_len = glsl_count_attribute_slots(type, false);

      num_inputs = MAX2(num_inputs, var->data.driver_location + array_len);
   }

   c->input_index_map = ralloc_array(c, struct rc_src_register, num_inputs);

   nir_foreach_shader_in_variable (var, c->s) {
      const struct glsl_type *type = var->type;
      unsigned array_len = glsl_count_attribute_slots(type, false);

      struct rc_src_register decl;

      decl = ntr_src_register(c, RC_FILE_INPUT, var->data.driver_location);

      if (var->data.location == VARYING_SLOT_FACE) {
         struct rc_dst_register temp = ntr_temp(c);
         /* tgsi docs say that floating point FACE will be positive for
          * frontface and negative for backface, but realistically
          * GLSL-to-TGSI had been doing MOV_SAT to turn it into 0.0 vs 1.0.
          * Copy that behavior, since some drivers (r300) have been doing a
          * 0.0 vs 1.0 backface (and I don't think anybody has a non-1.0
          * front face).
          */
         rc_sub_instruction(c, RC_OPCODE_MOV, &temp, RC_SATURATE_ZERO_ONE,
                            &decl, NULL, NULL);
         decl = ntr_src_from_dst(temp);
      }

      for (unsigned i = 0; i < array_len; i++) {
         c->input_index_map[var->data.driver_location + i] = decl;
         ntr_set_src_index(c, &c->input_index_map[var->data.driver_location + i],
                           decl.Index + i);
      }
   }
}

static int
ntr_sort_by_location(const nir_variable *a, const nir_variable *b)
{
   return a->data.location - b->data.location;
}

/* Preallocate FS output registers in a deterministic order. */
static void
ntr_setup_outputs(struct ntr_compile *c)
{
   if (c->s->info.stage != MESA_SHADER_FRAGMENT)
      return;

   nir_sort_variables_with_modes(c->s, ntr_sort_by_location, nir_var_shader_out);

   nir_foreach_shader_out_variable (var, c->s) {
      ntr_fs_output_index(c, var->data.location, var->data.index);
   }
}

static rc_texture_target
rc_texture_target_from_sampler_dim(enum glsl_sampler_dim dim, bool is_array)
{
   /* r300 has no array, multisample or buffer textures. */
   assert(!is_array);
   switch (dim) {
   case GLSL_SAMPLER_DIM_1D:
      return RC_TEXTURE_1D;
   case GLSL_SAMPLER_DIM_2D:
   case GLSL_SAMPLER_DIM_EXTERNAL:
      return RC_TEXTURE_2D;
   case GLSL_SAMPLER_DIM_3D:
      return RC_TEXTURE_3D;
   case GLSL_SAMPLER_DIM_CUBE:
      return RC_TEXTURE_CUBE;
   case GLSL_SAMPLER_DIM_RECT:
      return RC_TEXTURE_RECT;
   default:
      UNREACHABLE("unknown sampler dim");
   }
}

static void
ntr_setup_uniforms(struct ntr_compile *c)
{
   /* lower_uniforms_to_ubo lowered non-sampler uniforms to UBOs. */
   unsigned size = 0;
   nir_foreach_variable_with_modes (var, c->s, nir_var_mem_ubo) {
      int ubo = var->data.driver_location;
      assert(ubo == 0 && size == 0);
      size = glsl_get_explicit_size(var->interface_type, false);
   }
   c->ubo_size = size ? DIV_ROUND_UP(size, 16) : 0;
}

static void
ntr_setup_registers(struct ntr_compile *c)
{
   assert(c->num_temps == 0);

   /* After that, allocate non-array regs in our virtual space that we'll
    * register-allocate before RC emit.
    */
   nir_foreach_reg_decl_safe (nir_reg, nir_shader_get_entrypoint(c->s)) {
      assert(nir_intrinsic_num_array_elems(nir_reg) == 0);
      unsigned num_components = nir_intrinsic_num_components(nir_reg);
      unsigned index = nir_reg->def.index;

      struct rc_dst_register decl;
      uint32_t write_mask = BITFIELD_MASK(num_components);

      if (!ntr_try_store_reg_in_tgsi_output(c, &decl, nir_reg)) {
         decl = ntr_writemask(ntr_temp(c), write_mask);
      }
      c->reg_temp[index] = decl;
   }
}

static struct rc_src_register
ntr_get_load_const_src(struct ntr_compile *c, nir_load_const_instr *instr)
{
   int num_components = instr->def.num_components;

   /* Always emit a full vec4 immediate per load_const and let the
    * RC backend's constant packing handle it later.
    */
   float values[4] = {0};
   assert(instr->def.bit_size == 32);
   for (int i = 0; i < num_components; i++)
      values[i] = uif(instr->value[i].u32);

   unsigned index = util_dynarray_num_elements(&c->immediates, struct ntr_immediate);
   struct ntr_immediate *slot =
      util_dynarray_grow(&c->immediates, struct ntr_immediate, 1);
   memcpy(slot->values, values, sizeof(values));
   return ntr_src_register(c, RC_FILE_CONSTANT, c->immediate_offset + index);
}

static struct rc_src_register
ntr_reladdr(struct ntr_compile *c, struct rc_src_register addr)
{
   ntr_ARL(c, c->addr_reg, addr);
   return ntr_scalar(ntr_src_from_dst(c->addr_reg), 0);
}

/* Forward declare for recursion with indirects */
static struct rc_src_register ntr_get_src(struct ntr_compile *c, nir_src src);

static struct rc_src_register
ntr_get_chased_src(struct ntr_compile *c, nir_legacy_src *src)
{
   if (src->is_ssa) {
      if (nir_def_is_const(src->ssa))
         return ntr_get_load_const_src(c, nir_def_as_load_const(src->ssa));

      return c->ssa_temp[src->ssa->index];
   } else {
      struct rc_dst_register reg_temp = c->reg_temp[src->reg.handle->index];
      ntr_set_dst_index(c, &reg_temp, reg_temp.Index + src->reg.base_offset);

      if (src->reg.indirect) {
         struct rc_src_register offset = ntr_get_src(c, nir_src_for_ssa(src->reg.indirect));
         ntr_reladdr(c, offset);
         return ntr_src_indirect(ntr_src_from_dst(reg_temp));
      } else {
         return ntr_src_from_dst(reg_temp);
      }
   }
}

static struct rc_src_register
ntr_get_src(struct ntr_compile *c, nir_src src)
{
   nir_legacy_src chased = nir_legacy_chase_src(&src);
   return ntr_get_chased_src(c, &chased);
}

static struct rc_src_register
ntr_get_alu_src(struct ntr_compile *c, nir_alu_instr *instr, int i)
{
   /* The RC source modifier fields we use here are 32-bit float modifiers.
    * Keep integer and f64 modifiers explicit in NIR.
    *
    * The lower_fabs requests that we not have native source modifiers
    * for fabs, and instead emit MAX(a,-a) for nir_op_fabs.
    */
   nir_legacy_alu_src src = nir_legacy_chase_alu_src(&instr->src[i], !c->lower_fabs);
   struct rc_src_register usrc = ntr_get_chased_src(c, &src.src);

   usrc = ntr_swizzle(usrc, src.swizzle[0], src.swizzle[1], src.swizzle[2], src.swizzle[3]);

   if (src.fabs)
      usrc = ntr_abs(usrc);
   if (src.fneg)
      usrc = ntr_negate(usrc);

   return usrc;
}

/* Reswizzles a source so that the unset channels in the write mask still refer
 * to one of the channels present in the write mask.
 */
static struct rc_src_register
ntr_swizzle_for_write_mask(struct rc_src_register src, uint32_t write_mask)
{
   assert(write_mask);
   int first_chan = ffs(write_mask) - 1;
   return ntr_swizzle(src, (write_mask & RC_MASK_X) ? RC_SWIZZLE_X : first_chan,
                      (write_mask & RC_MASK_Y) ? RC_SWIZZLE_Y : first_chan,
                      (write_mask & RC_MASK_Z) ? RC_SWIZZLE_Z : first_chan,
                      (write_mask & RC_MASK_W) ? RC_SWIZZLE_W : first_chan);
}

static struct rc_dst_register
ntr_get_ssa_def_decl(struct ntr_compile *c, nir_def *ssa)
{
   uint32_t writemask;
   /* Fix writemask for nir_intrinsic_load_ubo_vec4 according to uses. */
   if (nir_def_is_intrinsic(ssa) &&
       nir_def_as_intrinsic(ssa)->intrinsic == nir_intrinsic_load_ubo_vec4)
      writemask = nir_def_components_read(ssa);
   else
      writemask = BITSET_MASK(ssa->num_components);

   struct rc_dst_register dst;
   if (!ntr_try_store_ssa_in_tgsi_output(c, &dst, ssa))
      dst = ntr_temp(c);

   c->ssa_temp[ssa->index] = ntr_swizzle_for_write_mask(ntr_src_from_dst(dst), writemask);

   return ntr_writemask(dst, writemask);
}

static struct rc_dst_register
ntr_get_chased_dest_decl(struct ntr_compile *c, nir_legacy_dest *dest)
{
   if (dest->is_ssa)
      return ntr_get_ssa_def_decl(c, dest->ssa);
   else
      return c->reg_temp[dest->reg.handle->index];
}

static struct rc_dst_register
ntr_get_chased_dest(struct ntr_compile *c, nir_legacy_dest *dest)
{
   struct rc_dst_register dst = ntr_get_chased_dest_decl(c, dest);

   if (!dest->is_ssa) {
      ntr_set_dst_index(c, &dst, dst.Index + dest->reg.base_offset);

      if (dest->reg.indirect) {
         struct rc_src_register offset = ntr_get_src(c, nir_src_for_ssa(dest->reg.indirect));
         ntr_reladdr(c, offset);
         rc_error(c->compiler,
                  "r300: Relative addressing of destination operands is unsupported.\n");
      }
   }

   return dst;
}

static struct rc_dst_register
ntr_get_dest(struct ntr_compile *c, nir_def *def)
{
   nir_legacy_dest chased = nir_legacy_chase_dest(def);
   return ntr_get_chased_dest(c, &chased);
}

static struct ntr_alu_dst
ntr_get_alu_dest(struct ntr_compile *c, nir_def *def)
{
   nir_legacy_alu_dest chased = nir_legacy_chase_alu_dest(def);
   struct rc_dst_register dst = ntr_get_chased_dest(c, &chased.dest);
   rc_saturate_mode saturate =
      chased.fsat ? RC_SATURATE_ZERO_ONE : RC_SATURATE_NONE;

   /* Only registers get write masks */
   if (!chased.dest.is_ssa)
      dst = ntr_writemask(dst, chased.write_mask);

   return ntr_alu_dst(dst, saturate);
}

/* For an SSA dest being populated by a constant src, replace the storage with
 * a copy of the source reference.
 */
static void
ntr_store_def(struct ntr_compile *c, nir_def *def, struct rc_src_register src)
{
   if (!src.RelAddr) {
      switch (src.File) {
      case RC_FILE_INPUT:
      case RC_FILE_CONSTANT:
         c->ssa_temp[def->index] = src;
         return;
      default:
         break;
      }
   }

   ntr_MOV(c, ntr_get_ssa_def_decl(c, def), src);
}

static void
ntr_store(struct ntr_compile *c, nir_def *def, struct rc_src_register src)
{
   nir_legacy_dest chased = nir_legacy_chase_dest(def);

   if (chased.is_ssa)
      ntr_store_def(c, chased.ssa, src);
   else {
      struct rc_dst_register dst = ntr_get_chased_dest(c, &chased);
      ntr_MOV(c, dst, src);
   }
}

/* TODO: we should be probably scalarizing all the opcodes we need earlier in NIR. */
static void
ntr_emit_scalar(struct ntr_compile *c, rc_opcode op, struct ntr_alu_dst dst,
                struct rc_src_register src0,
                const struct rc_src_register *src1)
{
   /* POW is the only 2-operand scalar op. */
   bool has_src1 = op == RC_OPCODE_POW;
   assert(has_src1 == (src1 != NULL));

   for (unsigned i = 0; i < 4; i++) {
      if (dst.reg.WriteMask & (1 << i)) {
         struct ntr_alu_dst scalar_dst = dst;
         struct rc_src_register scalar_src0 = ntr_scalar(src0, i);
         struct rc_src_register scalar_src1;
         scalar_dst.reg = ntr_writemask(scalar_dst.reg, 1 << i);
         if (has_src1)
            scalar_src1 = ntr_scalar(*src1, i);
         ntr_emit_alu_op(c, op, scalar_dst, &scalar_src0,
                         has_src1 ? &scalar_src1 : NULL, NULL);
      }
   }
}

static void
ntr_emit_alu(struct ntr_compile *c, nir_alu_instr *instr)
{
   struct rc_src_register src[4];
   struct ntr_alu_dst dst;
   unsigned i;
   int num_srcs = nir_op_infos[instr->op].num_inputs;

   /* Don't try to translate folded fsat since their source won't be valid */
   if (instr->op == nir_op_fsat && nir_legacy_fsat_folds(instr))
      return;

   assert(num_srcs <= ARRAY_SIZE(src));
   for (i = 0; i < num_srcs; i++)
      src[i] = ntr_get_alu_src(c, instr, i);

   dst = ntr_get_alu_dest(c, &instr->def);

   static const rc_opcode op_map[] = {
      [nir_op_mov] = RC_OPCODE_MOV,

      [nir_op_fdot2_replicated] = RC_OPCODE_DP2,
      [nir_op_fdot3_replicated] = RC_OPCODE_DP3,
      [nir_op_fdot4_replicated] = RC_OPCODE_DP4,
      [nir_op_ffract] = RC_OPCODE_FRC,
      [nir_op_fround_even] = RC_OPCODE_ROUND,

      [nir_op_slt] = RC_OPCODE_SLT,
      [nir_op_sge] = RC_OPCODE_SGE,
      [nir_op_seq] = RC_OPCODE_SEQ,
      [nir_op_sne] = RC_OPCODE_SNE,

      [nir_op_fadd] = RC_OPCODE_ADD,
      [nir_op_fmul] = RC_OPCODE_MUL,

      [nir_op_fmin] = RC_OPCODE_MIN,
      [nir_op_fmax] = RC_OPCODE_MAX,
      [nir_op_fmad] = RC_OPCODE_MAD,
   };

   if (instr->op < ARRAY_SIZE(op_map) && op_map[instr->op] > 0) {
      /* The normal path for NIR to TGSI ALU op translation */
      const struct rc_opcode_info *opcode_info = rc_get_opcode_info(op_map[instr->op]);
      ntr_emit_alu_op(c, op_map[instr->op], dst,
                      opcode_info->NumSrcRegs > 0 ? &src[0] : NULL,
                      opcode_info->NumSrcRegs > 1 ? &src[1] : NULL,
                      opcode_info->NumSrcRegs > 2 ? &src[2] : NULL);
   } else {
      /* Special cases for NIR to TGSI ALU op translation. */

      /* TODO: Use something like the ntr_store() path for the MOV calls so we
       * don't emit extra MOVs for swizzles/srcmods of inputs/const/imm.
       */

      switch (instr->op) {
      case nir_op_fabs:
         /* Try to eliminate */
         if (!c->lower_fabs && nir_legacy_float_mod_folds(instr))
            break;

         if (c->lower_fabs)
            ntr_emit_alu_op2(c, RC_OPCODE_MAX, dst, src[0], ntr_negate(src[0]));
         else
            ntr_emit_alu_op1(c, RC_OPCODE_MOV, dst, ntr_abs(src[0]));
         break;

      case nir_op_fsat:
         ntr_emit_alu_op1(c, RC_OPCODE_MOV, ntr_alu_dst_saturate(dst), src[0]);
         break;

      case nir_op_fneg:
         /* Try to eliminate */
         if (nir_legacy_float_mod_folds(instr))
            break;

         ntr_emit_alu_op1(c, RC_OPCODE_MOV, dst, ntr_negate(src[0]));
         break;

         /* NOTE: TGSI 32-bit math ops have the old "one source channel
          * replicated to all dst channels" behavior, while 64 is normal mapping
          * of src channels to dst.
          */
      case nir_op_frcp:
         ntr_emit_scalar(c, RC_OPCODE_RCP, dst, src[0], NULL);
         break;

      case nir_op_frsq:
         ntr_emit_scalar(c, RC_OPCODE_RSQ, dst, src[0], NULL);
         break;

      case nir_op_fexp2:
         ntr_emit_scalar(c, RC_OPCODE_EX2, dst, src[0], NULL);
         break;

      case nir_op_flog2:
         ntr_emit_scalar(c, RC_OPCODE_LG2, dst, src[0], NULL);
         break;

      case nir_op_fsin:
         ntr_emit_scalar(c, RC_OPCODE_SIN, dst, src[0], NULL);
         break;

      case nir_op_fcos:
         ntr_emit_scalar(c, RC_OPCODE_COS, dst, src[0], NULL);
         break;

      case nir_op_fsub:
         ntr_emit_alu_op2(c, RC_OPCODE_ADD, dst, src[0], ntr_negate(src[1]));
         break;

      case nir_op_fpow:
         ntr_emit_scalar(c, RC_OPCODE_POW, dst, src[0], &src[1]);
         break;

      case nir_op_fcsel:
         /* Implement this as CMP(-abs(src0), src1, src2). */
         ntr_emit_alu_op3(c, RC_OPCODE_CMP, dst,
                          ntr_negate(ntr_abs(src[0])), src[1], src[2]);
         break;

      case nir_op_fcsel_gt:
         ntr_emit_alu_op3(c, RC_OPCODE_CMP, dst,
                          ntr_negate(src[0]), src[1], src[2]);
         break;

      case nir_op_fcsel_ge:
         /* Implement this as if !(src0 < 0.0) was identical to src0 >= 0.0. */
         ntr_emit_alu_op3(c, RC_OPCODE_CMP, dst, src[0], src[2], src[1]);
         break;

      default:
         fprintf(stderr, "Unknown NIR opcode: %s\n", nir_op_infos[instr->op].name);
         UNREACHABLE("Unknown NIR opcode");
      }
   }
}

static struct rc_src_register
ntr_src_offset(struct ntr_compile *c, struct rc_src_register nsrc, nir_src src)
{
   if (nir_src_is_const(src)) {
      ntr_set_src_index(c, &nsrc, nsrc.Index + ntr_src_as_uint(c, src));
      return nsrc;
   } else {
      ntr_reladdr(c, ntr_get_src(c, src));
      return ntr_src_indirect(nsrc);
   }
}

static struct rc_dst_register
ntr_dst_offset(struct ntr_compile *c, struct rc_dst_register dst, nir_src src)
{
   /* Indirect adressing should have been lowered earlier in NIR. */
   assert(nir_src_is_const(src));
   ntr_set_dst_index(c, &dst, dst.Index + ntr_src_as_uint(c, src));
   return dst;
}

/* Some load operations in NIR will have a fractional offset that we need to
 * swizzle down before storing to the result register.
 */
static struct rc_src_register
ntr_shift_by_frac(struct rc_src_register src, unsigned frac, unsigned num_components)
{
   return ntr_swizzle(src, frac, frac + MIN2(num_components - 1, 1),
                      frac + MIN2(num_components - 1, 2), frac + MIN2(num_components - 1, 3));
}

static void
ntr_emit_load_ubo(struct ntr_compile *c, nir_intrinsic_instr *instr)
{
   struct rc_src_register src = ntr_src_register(c, RC_FILE_CONSTANT, 0);

   /* r300 only exposes a single UBO and any indirect UBO array indexing
    * has been lowered before we get here. */
   assert(nir_src_is_const(instr->src[0]));
   assert(ntr_src_as_uint(c, instr->src[0]) == 0);

   /* !pipe_caps.load_constbuf: Just emit it as a vec4 reference to the const
    * file.
    */
   ntr_set_src_index(c, &src, nir_intrinsic_base(instr));

   if (nir_src_is_const(instr->src[1])) {
      ntr_set_src_index(c, &src, src.Index + ntr_src_as_uint(c, instr->src[1]));
   } else {
      ntr_reladdr(c, ntr_get_src(c, instr->src[1]));
      src = ntr_src_indirect(src);
   }

   int start_component = nir_intrinsic_component(instr);

   src = ntr_shift_by_frac(src, start_component, instr->num_components);

   ntr_store(c, &instr->def, src);
}

static void
ntr_emit_load_uniform(struct ntr_compile *c, nir_intrinsic_instr *instr)
{
   unsigned index = nir_intrinsic_base(instr);
   assert(index < util_dynarray_num_elements(&c->state_constants,
                                             struct ntr_state_constant));
   assert(nir_src_is_const(instr->src[0]));
   assert(ntr_src_as_uint(c, instr->src[0]) == 0);

   struct rc_src_register src =
      ntr_src_register(c, RC_FILE_CONSTANT, c->state_offset + index);
   ntr_store(c, &instr->def, src);
}

static void
ntr_emit_load_input(struct ntr_compile *c, nir_intrinsic_instr *instr)
{
   uint32_t frac = nir_intrinsic_component(instr);
   uint32_t num_components = instr->num_components;
   unsigned base = nir_intrinsic_base(instr);
   struct rc_src_register input;
   nir_io_semantics semantics = nir_intrinsic_io_semantics(instr);

   if (c->s->info.stage == MESA_SHADER_FRAGMENT)
      ntr_read_input_output(c, semantics.location, base);

   if (c->s->info.stage == MESA_SHADER_VERTEX) {
      input = ntr_src_register(c, RC_FILE_INPUT, base);
   } else {
      input = c->input_index_map[base];
   }

   input = ntr_shift_by_frac(input, frac, num_components);

   switch (instr->intrinsic) {
   case nir_intrinsic_load_input:
      input = ntr_src_offset(c, input, instr->src[0]);
      ntr_store(c, &instr->def, input);
      break;

   case nir_intrinsic_load_interpolated_input: {
      input = ntr_src_offset(c, input, instr->src[1]);

      nir_intrinsic_instr *bary_instr = nir_def_as_intrinsic(instr->src[0].ssa);

      switch (bary_instr->intrinsic) {
      case nir_intrinsic_load_barycentric_pixel:
         /* The barycentric load matches the interpolation on the input
          * declaration, so we can use it directly.
          */
         ntr_store(c, &instr->def, input);
         break;

      case nir_intrinsic_load_barycentric_centroid:
         /* On r300 interpolation is fixed by the rasterizer state; the NIR
          * lowering pairs centroid intrinsics with centroid-declared inputs,
          * so we never need an explicit INTERP instruction. */
         ntr_store(c, &instr->def, input);
         break;

      default:
         UNREACHABLE("bad barycentric interp intrinsic\n");
      }
      break;
   }

   default:
      UNREACHABLE("bad load input intrinsic\n");
   }
}

static void
ntr_emit_store_output(struct ntr_compile *c, nir_intrinsic_instr *instr)
{
   struct rc_src_register src = ntr_get_src(c, instr->src[0]);

   if (src.File == RC_FILE_OUTPUT) {
      /* If our src is the output file, that's an indication that we were able
       * to emit the output stores in the generating instructions and we have
       * nothing to do here.
       */
      return;
   }

   uint32_t frac;
   struct rc_dst_register out = ntr_output_decl(c, instr, &frac);

   out = ntr_dst_offset(c, out, instr->src[1]);

   uint8_t swizzle[4] = {0, 0, 0, 0};
   for (int i = frac; i < 4; i++) {
      if (out.WriteMask & (1 << i))
         swizzle[i] = i - frac;
   }

   src = ntr_swizzle(src, swizzle[0], swizzle[1], swizzle[2], swizzle[3]);

   ntr_MOV(c, out, src);
}

static void
ntr_emit_intrinsic(struct ntr_compile *c, nir_intrinsic_instr *instr)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_ubo_vec4:
      ntr_emit_load_ubo(c, instr);
      break;

   case nir_intrinsic_load_uniform:
      ntr_emit_load_uniform(c, instr);
      break;

   case nir_intrinsic_load_frag_coord:
   case nir_intrinsic_load_point_coord:
   case nir_intrinsic_load_front_face:
      UNREACHABLE("system value should have been lowered to a varying");
      break;

   case nir_intrinsic_load_input:
   case nir_intrinsic_load_interpolated_input:
      ntr_emit_load_input(c, instr);
      break;

   case nir_intrinsic_store_output:
      ntr_emit_store_output(c, instr);
      break;

   case nir_intrinsic_terminate:
      ntr_KILL(c);
      break;

   case nir_intrinsic_terminate_if: {
      struct rc_src_register cond = ntr_scalar(ntr_get_src(c, instr->src[0]), 0);
      /* For !native_integers, the bool got lowered to 1.0 or 0.0. */
      ntr_KILL_IF(c, ntr_negate(cond));
      break;
   }
      /* In TGSI we don't actually generate the barycentric coords, and
       * emit the corresponding INTERP_CENTROID instruction in
       * ntr_emit_load_input. The barycentric loads themselves are
       * therefore consumed there.
       */
   case nir_intrinsic_load_barycentric_pixel:
   case nir_intrinsic_load_barycentric_centroid:
      break;

   case nir_intrinsic_ddx:
   case nir_intrinsic_ddx_coarse:
   case nir_intrinsic_ddy:
   case nir_intrinsic_ddy_coarse: {
      bool is_ddx = instr->intrinsic == nir_intrinsic_ddx ||
                    instr->intrinsic == nir_intrinsic_ddx_coarse;
      rc_opcode opcode = is_ddx ? RC_OPCODE_DDX : RC_OPCODE_DDY;
      struct rc_dst_register dst = ntr_get_dest(c, &instr->def);
      struct rc_src_register src0 = ntr_get_src(c, instr->src[0]);
      /* r500 MDH/MDV takes the form A*B+C; B=-1 encodes as src1 = {1111, negate}. */
      struct rc_src_register src1 = {.Swizzle = RC_SWIZZLE_1111, .Negate = RC_MASK_XYZW};
      rc_sub_instruction(c, opcode, &dst, RC_SATURATE_NONE, &src0, &src1, NULL);
      return;
   }

   case nir_intrinsic_decl_reg:
   case nir_intrinsic_load_reg:
   case nir_intrinsic_load_reg_indirect:
   case nir_intrinsic_store_reg:
   case nir_intrinsic_store_reg_indirect:
      /* fully consumed */
      break;

   default:
      fprintf(stderr, "Unknown intrinsic: ");
      nir_print_instr(&instr->instr, stderr);
      fprintf(stderr, "\n");
      break;
   }
}

struct ntr_tex_operand_state {
   struct rc_src_register srcs[3];
   unsigned i;
};

static void
ntr_push_tex_arg(struct ntr_compile *c, nir_tex_instr *instr, nir_tex_src_type tex_src_type,
                 struct ntr_tex_operand_state *s)
{
   int tex_src = nir_tex_instr_src_index(instr, tex_src_type);
   if (tex_src < 0)
      return;

   nir_src *src = &instr->src[tex_src].src;
   s->srcs[s->i++] = ntr_get_src(c, *src);
}

static void
ntr_emit_texture(struct ntr_compile *c, nir_tex_instr *instr)
{
   struct rc_dst_register dst = ntr_get_dest(c, &instr->def);
   struct rc_dst_register tex_dst = dst;
   assert(!instr->is_shadow);
   rc_texture_target target =
      rc_texture_target_from_sampler_dim(instr->sampler_dim, instr->is_array);
   unsigned tex_opcode;

   int tex_handle_src = nir_tex_instr_src_index(instr, nir_tex_src_texture_handle);
   int sampler_handle_src = nir_tex_instr_src_index(instr, nir_tex_src_sampler_handle);

   unsigned sampler_index = instr->sampler_index;
   bool sampler_reladdr = false;
   if (tex_handle_src >= 0 && sampler_handle_src >= 0) {
      /* It seems we can't get separate tex/sampler on GL, just use one of the handles */
      struct rc_src_register sampler = ntr_get_src(c, instr->src[tex_handle_src].src);
      sampler_index = sampler.Index;
      sampler_reladdr = sampler.RelAddr;
      assert(nir_tex_instr_src_index(instr, nir_tex_src_sampler_offset) == -1);
   } else {
      assert(tex_handle_src == -1 && sampler_handle_src == -1);
      int sampler_src = nir_tex_instr_src_index(instr, nir_tex_src_sampler_offset);
      if (sampler_src >= 0) {
         struct rc_src_register reladdr = ntr_get_src(c, instr->src[sampler_src].src);
         ntr_reladdr(c, reladdr);
         sampler_reladdr = true;
      }
   }

   switch (instr->op) {
   case nir_texop_tex:
      if (nir_tex_instr_src_size(instr, nir_tex_instr_src_index(instr, nir_tex_src_backend1)) >
          MAX2(instr->coord_components, 2) + instr->is_shadow)
         tex_opcode = RC_OPCODE_TXP;
      else
         tex_opcode = RC_OPCODE_TEX;
      break;
   case nir_texop_txl:
      tex_opcode = RC_OPCODE_TXL;
      break;
   case nir_texop_txb:
      tex_opcode = RC_OPCODE_TXB;
      break;
   case nir_texop_txd:
      tex_opcode = RC_OPCODE_TXD;
      break;
   default:
      UNREACHABLE("unsupported tex op");
   }

   struct ntr_tex_operand_state s = {.i = 0};
   ntr_push_tex_arg(c, instr, nir_tex_src_backend1, &s);
   ntr_push_tex_arg(c, instr, nir_tex_src_backend2, &s);

   /* The RC backend doesn't have TEX2/TXB2/TXL2-style opcodes; the
    * shadow comparator that would need a second backend slot is
    * already lowered before nir_to_rc by nir_lower_tex_shadow. */
   assert(s.i <= 1);

   if (instr->op == nir_texop_txd) {
      /* Derivs appear in their own src args */
      int ddx = nir_tex_instr_src_index(instr, nir_tex_src_ddx);
      int ddy = nir_tex_instr_src_index(instr, nir_tex_src_ddy);
      s.srcs[s.i++] = ntr_get_src(c, instr->src[ddx].src);
      s.srcs[s.i++] = ntr_get_src(c, instr->src[ddy].src);
   }

   assert(s.i == rc_get_opcode_info(tex_opcode)->NumSrcRegs);

   bool needs_mov =
      dst.File != RC_FILE_TEMPORARY ||
      (!c->compiler->is_r500 && dst.WriteMask != RC_MASK_XYZW);
   if (needs_mov)
      tex_dst = ntr_temp(c);

   struct rc_sub_instruction *inst =
      rc_sub_instruction(c, tex_opcode, &tex_dst, RC_SATURATE_NONE, &s.srcs[0],
               s.i > 1 ? &s.srcs[1] : NULL, s.i > 2 ? &s.srcs[2] : NULL);
   inst->TexSrcTarget = target;
   ntr_rc_tex_sampler(c, inst, sampler_index, sampler_reladdr);
   inst->TexSwizzle = RC_SWIZZLE_XYZW;

   if (needs_mov)
      ntr_MOV(c, dst, ntr_src_from_dst(tex_dst));
}

static void
ntr_emit_jump(struct ntr_compile *c, nir_jump_instr *jump)
{
   switch (jump->type) {
   case nir_jump_break:
      ntr_BRK(c);
      break;

   case nir_jump_continue:
      ntr_CONT(c);
      break;

   default:
      fprintf(stderr, "Unknown jump instruction: ");
      nir_print_instr(&jump->instr, stderr);
      fprintf(stderr, "\n");
      abort();
   }
}

static void
ntr_emit_ssa_undef(struct ntr_compile *c, nir_undef_instr *instr)
{
   /* Nothing to do but make sure that we have some storage to deref. */
   (void)ntr_get_ssa_def_decl(c, &instr->def);
}

static void
ntr_emit_instr(struct ntr_compile *c, nir_instr *instr)
{
   switch (instr->type) {
   case nir_instr_type_deref:
      /* ignored, will be walked by nir_intrinsic_image_*_deref. */
      break;

   case nir_instr_type_alu:
      ntr_emit_alu(c, nir_instr_as_alu(instr));
      break;

   case nir_instr_type_intrinsic:
      ntr_emit_intrinsic(c, nir_instr_as_intrinsic(instr));
      break;

   case nir_instr_type_load_const:
      /* Nothing to do here, as load consts are done directly from
       * ntr_get_src() (since many constant NIR srcs will often get folded
       * directly into a register file index instead of as a TGSI src).
       */
      break;

   case nir_instr_type_tex:
      ntr_emit_texture(c, nir_instr_as_tex(instr));
      break;

   case nir_instr_type_jump:
      ntr_emit_jump(c, nir_instr_as_jump(instr));
      break;

   case nir_instr_type_undef:
      ntr_emit_ssa_undef(c, nir_instr_as_undef(instr));
      break;

   default:
      fprintf(stderr, "Unknown NIR instr type: ");
      nir_print_instr(instr, stderr);
      fprintf(stderr, "\n");
      abort();
   }
}

static void
ntr_emit_if(struct ntr_compile *c, nir_if *if_stmt)
{
   ntr_IF(c, c->if_cond);

   ntr_emit_cf_list(c, &if_stmt->then_list);

   if (!nir_cf_list_is_empty_block(&if_stmt->else_list)) {
      ntr_ELSE(c);
      ntr_emit_cf_list(c, &if_stmt->else_list);
   }

   ntr_ENDIF(c);
}

static void
ntr_emit_loop(struct ntr_compile *c, nir_loop *loop)
{
   assert(!nir_loop_has_continue_construct(loop));
   ntr_BGNLOOP(c);
   ntr_emit_cf_list(c, &loop->body);
   ntr_ENDLOOP(c);
}

static void
ntr_emit_block(struct ntr_compile *c, nir_block *block)
{
   nir_foreach_instr (instr, block) {
      ntr_emit_instr(c, instr);
   }

   /* Set up the if condition for ntr_emit_if(). */
   nir_if *nif = nir_block_get_following_if(block);
   if (nif)
      c->if_cond = ntr_get_src(c, nif->condition);
}

static void
ntr_emit_cf_list(struct ntr_compile *c, struct exec_list *list)
{
   foreach_list_typed (nir_cf_node, node, node, list) {
      switch (node->type) {
      case nir_cf_node_block:
         ntr_emit_block(c, nir_cf_node_as_block(node));
         break;

      case nir_cf_node_if:
         ntr_emit_if(c, nir_cf_node_as_if(node));
         break;

      case nir_cf_node_loop:
         ntr_emit_loop(c, nir_cf_node_as_loop(node));
         break;

      default:
         UNREACHABLE("unknown CF type");
      }
   }
}

static void
ntr_add_constants(struct ntr_compile *c)
{
   /* Add UBO constants first (they show up before immediates in the
    * compiler's constants table; the immediate offset compensates for
    * this). */
   for (unsigned i = 0; i < c->ubo_size; i++) {
      struct rc_constant constant;
      memset(&constant, 0, sizeof(constant));
      constant.Type = RC_CONSTANT_EXTERNAL;
      constant.UseMask = RC_MASK_XYZW;
      constant.u.External = i;
      rc_constants_add(&c->compiler->Program.Constants, &constant);
   }
   assert(c->compiler->Program.Constants.Count == c->state_offset);

   util_dynarray_foreach (&c->state_constants, struct ntr_state_constant, state) {
      struct rc_constant constant;
      memset(&constant, 0, sizeof(constant));
      constant.Type = RC_CONSTANT_STATE;
      constant.UseMask = RC_MASK_XYZW;
      constant.u.State[0] = state->rc_state;
      constant.u.State[1] = state->sampler;
      rc_constants_add(&c->compiler->Program.Constants, &constant);
   }
   assert(c->compiler->Program.Constants.Count == c->immediate_offset);

   util_dynarray_foreach (&c->immediates, struct ntr_immediate, imm) {
      struct rc_constant constant;
      constant.Type = RC_CONSTANT_IMMEDIATE;
      constant.UseMask = RC_MASK_XYZW;
      for (unsigned j = 0; j < 4; j++)
         constant.u.Immediate[j] = imm->values[j];
      rc_constants_add(&c->compiler->Program.Constants, &constant);
   }
}

static void
ntr_emit_impl(struct ntr_compile *c, nir_function_impl *impl)
{
   c->ssa_temp = rzalloc_array(c, struct rc_src_register, impl->ssa_alloc);
   c->reg_temp = rzalloc_array(c, struct rc_dst_register, impl->ssa_alloc);

   ntr_setup_registers(c);

   ntr_setup_uniforms(c);
   c->state_offset = c->compiler->Program.Constants.Count + c->ubo_size;
   c->immediate_offset =
      c->state_offset + util_dynarray_num_elements(&c->state_constants,
                                                   struct ntr_state_constant);
   ntr_setup_inputs(c);
   ntr_setup_outputs(c);

   /* Emit RC instructions directly. */
   ntr_emit_cf_list(c, &impl->body);

   /* Add constants referenced by the emitted RC instructions. */
   ntr_add_constants(c);

}

static unsigned
type_size(const struct glsl_type *type, bool bindless)
{
   return glsl_count_attribute_slots(type, false);
}

/* Allow vectorizing of ALU instructions.
 */
static uint8_t
ntr_should_vectorize_instr(const nir_instr *instr, const void *data)
{
   if (instr->type != nir_instr_type_alu)
      return 0;

   return 4;
}

struct ntr_lower_backend_tex_state {
   struct ntr_compile *c;
   const struct r300_fragment_program_external_state *fs_state;
   bool is_r500;
};

static bool
ntr_lower_wpos_instr(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_interpolated_input)
      return false;

   nir_io_semantics semantics = nir_intrinsic_io_semantics(intr);
   if (semantics.location != VARYING_SLOT_POS)
      return false;

   struct ntr_compile *c = data;
   assert(intr->def.bit_size == 32);

   b->cursor = nir_after_instr(&intr->instr);
   nir_def *raw_wpos =
      nir_load_interpolated_input(b, 4, 32, intr->src[0].ssa, intr->src[1].ssa,
                                  .base = nir_intrinsic_base(intr),
                                  .component = 0,
                                  .io_semantics = semantics,
                                  .dest_type = nir_type_float32);

   nir_def *rcp_w = nir_frcp(b, nir_channel(b, raw_wpos, 3));
   nir_def *xyz = nir_fmul(b, nir_channels(b, raw_wpos, nir_component_mask(3)), rcp_w);
   nir_def *scale = ntr_load_state_constant(c, b, RC_STATE_R300_VIEWPORT_SCALE, 0, 4);
   nir_def *offset_state =
      ntr_load_state_constant(c, b, RC_STATE_R300_VIEWPORT_OFFSET, 0, 4);
   xyz = nir_fadd(b, nir_fmul(b, xyz, nir_channels(b, scale, nir_component_mask(3))),
                  nir_channels(b, offset_state, nir_component_mask(3)));

   nir_def *wpos = nir_vec4(b, nir_channel(b, xyz, 0), nir_channel(b, xyz, 1),
                            nir_channel(b, xyz, 2), rcp_w);

   unsigned component = nir_intrinsic_component(intr);
   nir_def *replacement =
      nir_channels(b, wpos, nir_component_mask(intr->num_components) << component);
   nir_def_rewrite_uses_after(&intr->def, replacement);
   return true;
}

static bool
ntr_lower_wpos(nir_shader *s, struct ntr_compile *c)
{
   return nir_shader_intrinsics_pass(s, ntr_lower_wpos_instr,
                                     nir_metadata_control_flow, c);
}

static nir_def *
ntr_tex_coord_replace_xyz(nir_builder *b, nir_def *coord, nir_def *xyz)
{
   if (coord->num_components <= 3)
      return xyz;

   assert(coord->num_components == 4);
   xyz = nir_pad_vector(b, xyz, 4);
   return nir_vector_insert_imm(b, xyz, nir_channel(b, coord, 3), 3);
}

static nir_def *
ntr_lower_backend_tex_wrap(nir_builder *b, nir_def *coord, rc_wrap_mode wrapmode)
{
   nir_def *xyz =
      nir_channels(b, coord, BITFIELD_MASK(MIN2(coord->num_components, 3)));

   switch (wrapmode) {
   case RC_WRAP_REPEAT:
      /* Texture wrap modes don't work on NPOT textures. Non-wrapped
       * texcoords are free in HW, but repeat needs coordinates in [0, 1].
       */
      xyz = nir_ffract(b, xyz);
      break;

   case RC_WRAP_MIRRORED_REPEAT:
      /* Mirroring repeats in [0, 2]. Scale to [0, 1], make the pattern
       * repeat, move it to [-1, 1], then use abs to mirror and reverse it:
       * 1 - abs(fract(v * 0.5) * 2 - 1).
       */
      xyz = nir_fmul_imm(b, xyz, 0.5f);
      xyz = nir_ffract(b, xyz);
      xyz = nir_fadd_imm(b, nir_fmul_imm(b, xyz, 2.0f), -1.0f);
      xyz = nir_fsub(b, nir_imm_floatN_t(b, 1.0f, xyz->bit_size),
                     nir_fabs(b, xyz));
      break;

   case RC_WRAP_MIRRORED_CLAMP:
      /* Mirror [0, 1] into [-1, 0] using abs. This works for CLAMP,
       * CLAMP_TO_EDGE, and CLAMP_TO_BORDER.
       */
      xyz = nir_fabs(b, xyz);
      break;

   default:
      UNREACHABLE("unknown backend texture wrap mode");
   }

   return ntr_tex_coord_replace_xyz(b, coord, xyz);
}

static bool
ntr_lower_backend_tex_instr(nir_builder *b, nir_tex_instr *tex, void *data)
{
   bool progress = false;
   struct ntr_lower_backend_tex_state *state = data;

   assert(tex->op == nir_texop_tex || tex->op == nir_texop_txb ||
          tex->op == nir_texop_txl || tex->op == nir_texop_txd);

   unsigned sampler = tex->sampler_index;
   assert(sampler < ARRAY_SIZE(state->fs_state->unit));

   const rc_wrap_mode wrapmode = state->fs_state->unit[sampler].wrap_mode;
   const bool clamp_scale =
      state->fs_state->unit[sampler].clamp_and_scale_before_fetch;
   const bool is_rect = tex->sampler_dim == GLSL_SAMPLER_DIM_RECT;

   b->cursor = nir_before_instr(&tex->instr);
   nir_def *coord = nir_get_tex_src(tex, nir_tex_src_coord);

   /* R300 cannot sample from rectangles, and the wrap fallback needs
    * normalized coordinates even on R500.
    */
   if (is_rect && (!state->is_r500 || wrapmode != RC_WRAP_NONE)) {
      nir_def *factor =
         ntr_load_state_constant(state->c, b, RC_STATE_R300_TEXRECT_FACTOR,
                                 sampler, coord->num_components);
      coord = nir_fmul(b, coord, factor);
      tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
      progress = true;
   }

   /* When we emulate wrap or clamp/scale in ALU, projection has to happen
    * before that emulation.
    */
   if (wrapmode == RC_WRAP_REPEAT || wrapmode == RC_WRAP_MIRRORED_REPEAT ||
       clamp_scale) {
      nir_def *projector = nir_steal_tex_src(tex, nir_tex_src_projector);
      if (projector) {
         coord = nir_fmul(b, coord, nir_frcp(b, projector));
         progress = true;
      }
   }

   if (wrapmode != RC_WRAP_NONE) {
      coord = ntr_lower_backend_tex_wrap(b, coord, wrapmode);
      progress = true;
   }

   if (clamp_scale) {
      nir_def *xyz =
         nir_channels(b, coord, BITFIELD_MASK(MIN2(coord->num_components, 3)));
      coord = ntr_tex_coord_replace_xyz(b, coord, nir_fsat(b, xyz));

      nir_def *factor =
         ntr_load_state_constant(state->c, b, RC_STATE_R300_TEXSCALE_FACTOR,
                                 sampler, coord->num_components);
      coord = nir_fmul(b, coord, factor);
      progress = true;
   }

   if (progress) {
      int coord_index = nir_tex_instr_src_index(tex, nir_tex_src_coord);
      nir_src_rewrite(&tex->src[coord_index].src, coord);
   }

   return progress;
}

static bool
ntr_lower_backend_tex(nir_shader *s, struct ntr_compile *c,
                      const struct r300_fragment_program_external_state *fs_state,
                      bool is_r500)
{
   struct ntr_lower_backend_tex_state state = {
      .c = c,
      .fs_state = fs_state,
      .is_r500 = is_r500,
   };

   return nir_shader_tex_pass(s, ntr_lower_backend_tex_instr,
                              nir_metadata_control_flow, &state);
}

struct ntr_lower_tex_state {
   nir_scalar channels[8];
   unsigned i;
};

static void
nir_to_rc_lower_tex_instr_arg(nir_builder *b, nir_tex_instr *instr, nir_tex_src_type tex_src_type,
                              struct ntr_lower_tex_state *s)
{
   int tex_src = nir_tex_instr_src_index(instr, tex_src_type);
   if (tex_src < 0)
      return;

   nir_def *def = instr->src[tex_src].src.ssa;
   for (int i = 0; i < def->num_components; i++) {
      s->channels[s->i++] = nir_get_scalar(def, i);
   }

   nir_tex_instr_remove_src(instr, tex_src);
}

/**
 * Merges together a vec4 of tex coordinate/compare/bias/lod into a backend tex
 * src.  This lets NIR handle the coalescing of the vec4 rather than trying to
 * manage it on our own, and may lead to more vectorization.
 */
static bool
nir_to_rc_lower_tex_instr(nir_builder *b, nir_tex_instr *tex, void *data)
{
   if (nir_tex_instr_src_index(tex, nir_tex_src_coord) < 0)
      return false;

   b->cursor = nir_before_instr(&tex->instr);

   struct ntr_lower_tex_state s = {0};

   nir_to_rc_lower_tex_instr_arg(b, tex, nir_tex_src_coord, &s);
   /* We always have at least two slots for the coordinate, even on 1D. */
   s.i = MAX2(s.i, 2);

   nir_to_rc_lower_tex_instr_arg(b, tex, nir_tex_src_comparator, &s);
   s.i = MAX2(s.i, 3);

   nir_to_rc_lower_tex_instr_arg(b, tex, nir_tex_src_bias, &s);

   /* XXX: LZ */
   nir_to_rc_lower_tex_instr_arg(b, tex, nir_tex_src_lod, &s);
   nir_to_rc_lower_tex_instr_arg(b, tex, nir_tex_src_projector, &s);
   nir_to_rc_lower_tex_instr_arg(b, tex, nir_tex_src_ms_index, &s);

   /* No need to pack undefs in unused channels of the tex instr */
   while (!s.channels[s.i - 1].def)
      s.i--;

   /* Instead of putting undefs in the unused slots of the vecs, just put in
    * another used channel.  Otherwise, we'll get unnecessary moves into
    * registers.
    */
   assert(s.channels[0].def != NULL);
   for (int i = 1; i < s.i; i++) {
      if (!s.channels[i].def)
         s.channels[i] = s.channels[0];
   }

   nir_tex_instr_add_src(tex, nir_tex_src_backend1, nir_vec_scalars(b, s.channels, MIN2(s.i, 4)));
   if (s.i > 4)
      nir_tex_instr_add_src(tex, nir_tex_src_backend2, nir_vec_scalars(b, &s.channels[4], s.i - 4));

   return true;
}

static bool
nir_to_rc_lower_tex(nir_shader *s)
{
   return nir_shader_tex_pass(s, nir_to_rc_lower_tex_instr,
                              nir_metadata_control_flow, NULL);
}

/* Lowers texture projectors if we can't do them as RC_OPCODE_TXP. */
static void
nir_to_rc_lower_txp(nir_shader *s)
{
   nir_lower_tex_options lower_tex_options = {
      .lower_txp = 0,
   };

   nir_foreach_block (block, nir_shader_get_entrypoint(s)) {
      nir_foreach_instr (instr, block) {
         if (instr->type != nir_instr_type_tex)
            continue;
         nir_tex_instr *tex = nir_instr_as_tex(instr);

         if (nir_tex_instr_src_index(tex, nir_tex_src_projector) < 0)
            continue;

         bool has_compare = nir_tex_instr_src_index(tex, nir_tex_src_comparator) >= 0;
         bool has_lod = nir_tex_instr_src_index(tex, nir_tex_src_lod) >= 0 ||
                        s->info.stage != MESA_SHADER_FRAGMENT;
         bool has_offset = nir_tex_instr_src_index(tex, nir_tex_src_offset) >= 0;

         /* We can do TXP for any tex (not txg) where we can fit all the
          * coordinates and comparator and projector in one vec4 without any
          * other modifiers to add on.
          *
          * nir_lower_tex() only handles the lowering on a sampler-dim basis, so
          * if we get any funny projectors then we just blow them all away.
          */
         if (tex->op != nir_texop_tex || has_lod || has_offset ||
             (tex->coord_components >= 3 && has_compare))
            lower_tex_options.lower_txp |= 1 << tex->sampler_dim;
      }
   }

   /* nir_lower_tex must be run even if no options are set, because we need the
    * LOD to be set for query_levels and for non-fragment shaders.
    */
   NIR_PASS(_, s, nir_lower_tex, &lower_tex_options);
}

/* There are some issues with the tgsi_texcoord = false support in the state
 * tracker, specifically we are still getting texcoords and pointcoord in some cases
 * and ocassionally (with fixed function shaders) there are some inconsistencies
 * like vs using generics and fs using texcoords. This function tries to fix it.
 * See https://gitlab.freedesktop.org/mesa/mesa/-/issues/12749 for more details.
 */
void
ntr_fixup_varying_slots(nir_shader *s, nir_variable_mode mode)
{
   if (s->info.name && !strcmp(s->info.name, "st/drawtex VS"))
      return;

   nir_foreach_variable_with_modes (var, s, mode) {
      if (var->data.location >= VARYING_SLOT_VAR0 && var->data.location < VARYING_SLOT_PATCH0) {
         var->data.location += 9;
      } else if (var->data.location == VARYING_SLOT_PNTC) {
         var->data.location = VARYING_SLOT_VAR8;
      } else if ((var->data.location >= VARYING_SLOT_TEX0) &&
                 (var->data.location <= VARYING_SLOT_TEX7)) {
         var->data.location += VARYING_SLOT_VAR0 - VARYING_SLOT_TEX0;
      }
   }
}

static bool
r300_nir_lower_alpha_to_one_instr(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_store_output)
      return false;

   nir_io_semantics semantics = nir_intrinsic_io_semantics(intr);
   if (mesa_frag_result_get_color_index(semantics.location) < 0)
      return false;

   unsigned component = nir_intrinsic_component(intr);
   if (component > 3)
      return false;

   nir_def *src = intr->src[0].ssa;
   unsigned num_comp = src->num_components;
   unsigned alpha_src_idx = 3 - component;
   if (alpha_src_idx >= num_comp ||
       !(nir_intrinsic_write_mask(intr) & BITFIELD_BIT(alpha_src_idx)))
      return false;

   b->cursor = nir_before_instr(&intr->instr);
   nir_def *one = nir_imm_floatN_t(b, 1.0f, src->bit_size);
   nir_src_rewrite(&intr->src[0], nir_vector_insert_imm(b, src, one, alpha_src_idx));
   return true;
}

/* The common nir_lower_alpha_to_one() only handles DATA outputs with
 * full-vec4 sources. r300 also needs FRAG_RESULT_COLOR and partial stores.
 */
static bool
r300_nir_lower_alpha_to_one(nir_shader *s)
{
   return nir_shader_intrinsics_pass(s, r300_nir_lower_alpha_to_one_instr,
                                     nir_metadata_control_flow, NULL);
}

/**
 * Translates the NIR shader to RC instructions on the given compiler.
 *
 * This requires some lowering of the NIR shader to prepare it for
 * translation. We take ownership of the NIR shader passed; if you need to
 * keep the NIR, then pass us a clone.
 */
void
nir_to_rc(struct nir_shader *s, struct pipe_screen *screen,
          struct r300_fragment_program_external_state state,
          union r300_shader_code rc, struct radeon_compiler *compiler)
{
   struct ntr_compile *c;
   bool is_r500 = r300_screen(screen)->caps.is_r500;
   c = rzalloc(NULL, struct ntr_compile);
   c->compiler = compiler;
   c->lower_fabs = !is_r500 && s->info.stage == MESA_SHADER_VERTEX;
   c->addr_reg = ntr_writemask(ntr_dst_register(c, RC_FILE_ADDRESS, 0), RC_MASK_X);
   util_dynarray_init(&c->immediates, c);
   util_dynarray_init(&c->state_constants, c);
   for (unsigned i = 0; i < ARRAY_SIZE(c->fs_output_color_index); i++)
      c->fs_output_color_index[i] = -1;
   c->fs_output_depth_index = -1;
   for (unsigned i = 0; i < ARRAY_SIZE(c->fs_output_index); i++)
      c->fs_output_index[i] = -1;
   if (s->info.stage == MESA_SHADER_FRAGMENT) {
      c->semantics = &rc.f->inputs;
   } else {
      c->semantics = &rc.v->outputs;
   }

   if (s->info.stage == MESA_SHADER_FRAGMENT) {
      static const nir_lower_sysvals_to_varyings_options sysval_options = {
         .frag_coord = true,
         .point_coord = true,
         .front_face = true,
      };
      NIR_PASS(_, s, nir_lower_sysvals_to_varyings, &sysval_options);
   }

   ntr_fixup_varying_slots(s, s->info.stage == MESA_SHADER_FRAGMENT ? nir_var_shader_in : nir_var_shader_out);

   /* The fragment backend doesn't support relative addressing of input
    * sources, so lower array indexing on FS inputs before nir_lower_io turns
    * derefs into load_input offsets.
    */
   if (s->info.stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS(_, s, nir_lower_indirect_derefs_to_if_else_trees,
               nir_var_shader_in, UINT32_MAX);
      NIR_PASS(_, s, nir_remove_dead_variables, nir_var_shader_in, NULL);
   }

   NIR_PASS(_, s, nir_lower_io, nir_var_shader_in | nir_var_shader_out, type_size,
            nir_lower_io_use_interpolated_input_intrinsics);

   if (s->info.stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS(_, s, ntr_lower_wpos, c);

      /* Shadow lowering. */
      int num_texture_states = state.sampler_state_count;
      if (num_texture_states > 0) {
         nir_lower_tex_shadow_swizzle tex_swizzle[PIPE_MAX_SHADER_SAMPLER_VIEWS];
         enum compare_func tex_compare_func[PIPE_MAX_SHADER_SAMPLER_VIEWS];

         for (unsigned i = 0; i < num_texture_states; i++) {
            tex_compare_func[i] = state.unit[i].texture_compare_func;
            tex_swizzle[i].swizzle_r = GET_SWZ(state.unit[i].texture_swizzle, 0);
            tex_swizzle[i].swizzle_g = GET_SWZ(state.unit[i].texture_swizzle, 1);
            tex_swizzle[i].swizzle_b = GET_SWZ(state.unit[i].texture_swizzle, 2);
            tex_swizzle[i].swizzle_a = GET_SWZ(state.unit[i].texture_swizzle, 3);
         }
         NIR_PASS(_, s, nir_lower_tex_shadow, num_texture_states, tex_compare_func,
                  tex_swizzle, true);
      }

      NIR_PASS(_, s, ntr_lower_backend_tex, c, &state, is_r500);
      nir_to_rc_lower_txp(s);
      NIR_PASS(_, s, nir_to_rc_lower_tex);

      if (state.alpha_to_one)
         NIR_PASS(_, s, r300_nir_lower_alpha_to_one);
   }

   bool progress;
   do {
      progress = false;
      NIR_PASS(progress, s, nir_opt_algebraic);
      NIR_PASS(progress, s, nir_opt_constant_folding);
   } while (progress);

   do {
      progress = false;
      NIR_PASS(progress, s, nir_opt_algebraic_late);
      if (progress) {
         NIR_PASS(_, s, nir_opt_copy_prop);
         NIR_PASS(_, s, nir_opt_dce);
         NIR_PASS(_, s, nir_opt_cse);
      }
   } while (progress);

   if (s->info.stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS(_, s, r300_nir_prepare_presubtract);
   }

   NIR_PASS(_, s, nir_lower_int_to_float);
   NIR_PASS(_, s, nir_opt_copy_prop);
   NIR_PASS(_, s, r300_nir_post_integer_lowering);
   NIR_PASS(_, s, nir_lower_bool_to_float,
            is_r500 || s->info.stage == MESA_SHADER_FRAGMENT);
   /* bool_to_float generates MOVs for b2f32 that we want to clean up. */
   NIR_PASS(_, s, nir_opt_copy_prop);
   /* CSE cleanup after late ftrunc lowering. */
   NIR_PASS(_, s, nir_opt_cse);
   /* At this point we need to clean up:
    *  a) R300/R400 VS fcsel_ge from ftrunc and seq/sne from bool/int lowering,
    *  b) all flavours of fcsels that read three different temp sources on R500.
    */
   if (s->info.stage == MESA_SHADER_VERTEX) {
      if (is_r500)
         NIR_PASS(_, s, r300_nir_lower_fcsel_r500);
      else
         NIR_PASS(_, s, r300_nir_lower_vs_alu_r300);
      NIR_PASS(_, s, r300_nir_lower_flrp);
   } else {
      NIR_PASS(_, s, r300_nir_lower_comparison_fs);
   }
   NIR_PASS(_, s, r300_nir_opt_algebraic_late);
   NIR_PASS(_, s, nir_opt_dce);
   NIR_PASS(_, s, nir_opt_shrink_vectors, false);
   NIR_PASS(_, s, nir_opt_dce);

   nir_move_options move_all = nir_move_const_undef | nir_move_load_uniform |
                               nir_move_load_ubo | nir_move_load_input |
                               nir_move_load_frag_coord | nir_move_comparisons |
                               nir_move_copies | nir_move_load_ssbo;

   NIR_PASS(_, s, nir_opt_move, move_all);
   NIR_PASS(_, s, nir_move_vec_src_uses_to_dest, true);
   /* Late vectorizing after nir_move_vec_src_uses_to_dest helps instructions but
    * increases register usage. Testing shows this is beneficial only in VS.
    */
   if (s->info.stage == MESA_SHADER_VERTEX)
      NIR_PASS(_, s, nir_opt_vectorize, ntr_should_vectorize_instr, NULL);

   NIR_PASS(_, s, nir_convert_from_ssa, true, false);
   NIR_PASS(_, s, nir_lower_vec_to_regs, NULL, NULL);
   /* This cleans up duplicated scalar expressions exposed by lowering
    * vectors to registers. It does not help on R500.
    */
   if (!is_r500) {
      NIR_PASS(_, s, nir_opt_cse);
   }

   /* locals_to_reg_intrinsics will leave dead derefs that are good to clean up.
    */
   NIR_PASS(_, s, nir_lower_locals_to_regs, 32);
   NIR_PASS(_, s, nir_opt_dce);

   /* See comment in ntr_get_alu_src for supported modifiers */
   NIR_PASS(_, s, nir_legacy_trivialize, !c->lower_fabs);

   if (NIR_DEBUG(TGSI)) {
      fprintf(stderr, "NIR before translation to TGSI:\n");
      nir_print_shader(s, stderr);
   }

   c->s = s;
   /* Emit the main function */
   nir_function_impl *impl = nir_shader_get_entrypoint(c->s);
   ntr_emit_impl(c, impl);

   /* For FS, populate the FS-specific compiler outputs. */
   if (s->info.stage == MESA_SHADER_FRAGMENT) {
      struct r300_fragment_program_compiler *fc =
         (struct r300_fragment_program_compiler *)compiler;
      rc.f->uses_discard = s->info.fs.uses_discard;
      fc->OutputDepth = c->fs_output_depth_index >= 0 ? c->fs_output_depth_index
                                                      : c->num_outputs;
      for (unsigned i = 0; i < ARRAY_SIZE(c->fs_output_color_index); i++) {
         fc->OutputColor[i] = c->fs_output_color_index[i] >= 0
                                ? c->fs_output_color_index[i]
                                : c->num_outputs;
      }
   } else if (s->info.stage == MESA_SHADER_VERTEX) {
      rc.v->num_inputs = s->num_inputs;
   }

   rc_calculate_inputs_outputs(compiler);

   ralloc_free(c);
   ralloc_free(s);
}
