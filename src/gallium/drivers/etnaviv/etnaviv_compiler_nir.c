/*
 * Copyright (c) 2012-2019 Etnaviv Project
 * Copyright (c) 2019 Zodiac Inflight Innovations
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Jonathan Marek <jonathan@marek.ca>
 *    Wladimir J. van der Laan <laanwj@gmail.com>
 */

#include "etnaviv_compiler.h"
#include "etnaviv_asm.h"
#include "etnaviv_context.h"
#include "etnaviv_debug.h"
#include "etnaviv_disasm.h"
#include "etnaviv_uniforms.h"
#include "etnaviv_util.h"

#include <math.h>
#include "util/u_memory.h"
#include "util/register_allocate.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_worklist.h"

#include "tgsi/tgsi_strings.h"
#include "util/u_half.h"

struct etna_compile {
   nir_shader *nir;
#define is_fs(c) ((c)->nir->info.stage == MESA_SHADER_FRAGMENT)
   const struct etna_specs *specs;
   struct etna_shader_variant *variant;

   /* block # to instr index */
   unsigned *block_ptr;

   /* Code generation */
   int inst_ptr; /* current instruction pointer */
   struct etna_inst code[ETNA_MAX_INSTRUCTIONS * ETNA_INST_SIZE];

   /* constants */
   uint64_t consts[ETNA_MAX_IMM];

   /* There was an error during compilation */
   bool error;
};

/* io related lowering
 * run after lower_int_to_float because it adds i2f/f2i ops
 */
static void
etna_lower_io(nir_shader *shader, struct etna_shader_variant *v)
{
   nir_foreach_function(function, shader) {
      nir_builder b;
      nir_builder_init(&b, function->impl);

      nir_foreach_block(block, function->impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type == nir_instr_type_intrinsic) {
               nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

               switch (intr->intrinsic) {
               case nir_intrinsic_load_front_face: {
                  /* HW front_face is 0.0/1.0, not 0/~0u for bool
                   * lower with a comparison with 0
                   */
                  intr->dest.ssa.bit_size = 32;

                  b.cursor = nir_after_instr(instr);

                  nir_ssa_def *ssa = nir_ine(&b, &intr->dest.ssa, nir_imm_int(&b, 0));
                  if (v->key.front_ccw)
                     nir_instr_as_alu(ssa->parent_instr)->op = nir_op_ieq;

                  nir_ssa_def_rewrite_uses_after(&intr->dest.ssa,
                                                 nir_src_for_ssa(ssa),
                                                 ssa->parent_instr);
               } break;
               case nir_intrinsic_store_deref: {
                  if (shader->info.stage != MESA_SHADER_FRAGMENT || !v->key.frag_rb_swap)
                     break;

                  nir_deref_instr *deref = nir_src_as_deref(intr->src[0]);
                  assert(deref->deref_type == nir_deref_type_var);

                  if (deref->var->data.location != FRAG_RESULT_COLOR &&
                      deref->var->data.location != FRAG_RESULT_DATA0)
                      break;

                  b.cursor = nir_before_instr(instr);

                  nir_ssa_def *ssa = nir_mov(&b, intr->src[1].ssa);
                  nir_alu_instr *alu = nir_instr_as_alu(ssa->parent_instr);
                  alu->src[0].swizzle[0] = 2;
                  alu->src[0].swizzle[2] = 0;
                  nir_instr_rewrite_src(instr, &intr->src[1], nir_src_for_ssa(ssa));
               } break;
               case nir_intrinsic_load_uniform: {
                  /* multiply by 16 and convert to int */
                  b.cursor = nir_before_instr(instr);
                  nir_ssa_def *ssa = nir_imul(&b, intr->src[0].ssa, nir_imm_int(&b, 16));
                  nir_instr_rewrite_src(instr, &intr->src[0], nir_src_for_ssa(ssa));
               } break;
               default:
                  break;
               }
            }

            if (instr->type != nir_instr_type_tex)
               continue;

            nir_tex_instr *tex = nir_instr_as_tex(instr);
            nir_src *coord = NULL;
            nir_src *lod_bias = NULL;
            unsigned lod_bias_idx;

            assert(tex->sampler_index == tex->texture_index);

            for (unsigned i = 0; i < tex->num_srcs; i++) {
               switch (tex->src[i].src_type) {
               case nir_tex_src_coord:
                  coord = &tex->src[i].src;
                  break;
               case nir_tex_src_bias:
               case nir_tex_src_lod:
                  assert(!lod_bias);
                  lod_bias = &tex->src[i].src;
                  lod_bias_idx = i;
                  break;
               case nir_tex_src_comparator:
                  break;
               default:
                  assert(0);
                  break;
               }
            }

            if (tex->sampler_dim == GLSL_SAMPLER_DIM_RECT) {
               /* use a dummy load_uniform here to represent texcoord scale */
               b.cursor = nir_before_instr(instr);
               nir_intrinsic_instr *load =
                  nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_uniform);
               nir_intrinsic_set_base(load, ~tex->sampler_index);
               load->num_components = 2;
               load->src[0] = nir_src_for_ssa(nir_imm_float(&b, 0.0f));
               nir_ssa_dest_init(&load->instr, &load->dest, 2, 32, NULL);
               nir_intrinsic_set_type(load, nir_type_float);

               nir_builder_instr_insert(&b, &load->instr);

               nir_ssa_def *new_coord = nir_fmul(&b, coord->ssa, &load->dest.ssa);
               nir_instr_rewrite_src(&tex->instr, coord, nir_src_for_ssa(new_coord));
            }

            /* pre HALTI5 needs texture sources in a single source */

            if (!lod_bias || v->shader->specs->halti >= 5)
               continue;

            assert(coord && lod_bias && tex->coord_components < 4);

            nir_alu_instr *vec = nir_alu_instr_create(shader, nir_op_vec4);
            for (unsigned i = 0; i < tex->coord_components; i++) {
               vec->src[i].src = nir_src_for_ssa(coord->ssa);
               vec->src[i].swizzle[0] = i;
            }
            for (unsigned i = tex->coord_components; i < 4; i++)
               vec->src[i].src = nir_src_for_ssa(lod_bias->ssa);

            vec->dest.write_mask = 0xf;
            nir_ssa_dest_init(&vec->instr, &vec->dest.dest, 4, 32, NULL);

            nir_tex_instr_remove_src(tex, lod_bias_idx);
            nir_instr_rewrite_src(&tex->instr, coord, nir_src_for_ssa(&vec->dest.dest.ssa));
            tex->coord_components = 4;

            nir_instr_insert_before(&tex->instr, &vec->instr);
         }
      }
   }
}

static bool
etna_alu_to_scalar_filter_cb(const nir_instr *instr, const void *data)
{
   const struct etna_specs *specs = data;

   if (instr->type != nir_instr_type_alu)
      return false;

   nir_alu_instr *alu = nir_instr_as_alu(instr);
   switch (alu->op) {
   case nir_op_frsq:
   case nir_op_frcp:
   case nir_op_flog2:
   case nir_op_fexp2:
   case nir_op_fsqrt:
   case nir_op_fcos:
   case nir_op_fsin:
   case nir_op_fdiv:
   case nir_op_imul:
      return true;
   /* TODO: can do better than alu_to_scalar for vector compares */
   case nir_op_b32all_fequal2:
   case nir_op_b32all_fequal3:
   case nir_op_b32all_fequal4:
   case nir_op_b32any_fnequal2:
   case nir_op_b32any_fnequal3:
   case nir_op_b32any_fnequal4:
   case nir_op_b32all_iequal2:
   case nir_op_b32all_iequal3:
   case nir_op_b32all_iequal4:
   case nir_op_b32any_inequal2:
   case nir_op_b32any_inequal3:
   case nir_op_b32any_inequal4:
      return true;
   case nir_op_fdot2:
      if (!specs->has_halti2_instructions)
         return true;
      break;
   default:
      break;
   }

   return false;
}

static void
etna_lower_alu_impl(nir_function_impl *impl, struct etna_compile *c)
{
   nir_shader *shader = impl->function->shader;

   nir_builder b;
   nir_builder_init(&b, impl);

   /* in a seperate loop so we can apply the multiple-uniform logic to the new fmul */
   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_alu)
            continue;

         nir_alu_instr *alu = nir_instr_as_alu(instr);
         /* multiply sin/cos src by constant
          * TODO: do this earlier (but it breaks const_prop opt)
          */
         if (alu->op == nir_op_fsin || alu->op == nir_op_fcos) {
            b.cursor = nir_before_instr(instr);

            nir_ssa_def *imm = c->specs->has_new_transcendentals ?
               nir_imm_float(&b, 1.0 / M_PI) :
               nir_imm_float(&b, 2.0 / M_PI);

            nir_instr_rewrite_src(instr, &alu->src[0].src,
               nir_src_for_ssa(nir_fmul(&b, alu->src[0].src.ssa, imm)));
         }

         /* change transcendental ops to vec2 and insert vec1 mul for the result
          * TODO: do this earlier (but it breaks with optimizations)
          */
         if (c->specs->has_new_transcendentals && (
             alu->op == nir_op_fdiv || alu->op == nir_op_flog2 ||
             alu->op == nir_op_fsin || alu->op == nir_op_fcos)) {
            nir_ssa_def *ssa = &alu->dest.dest.ssa;

            assert(ssa->num_components == 1);

            nir_alu_instr *mul = nir_alu_instr_create(shader, nir_op_fmul);
            mul->src[0].src = mul->src[1].src = nir_src_for_ssa(ssa);
            mul->src[1].swizzle[0] = 1;

            mul->dest.write_mask = 1;
            nir_ssa_dest_init(&mul->instr, &mul->dest.dest, 1, 32, NULL);

            ssa->num_components = 2;

            mul->dest.saturate = alu->dest.saturate;
            alu->dest.saturate = 0;

            nir_instr_insert_after(instr, &mul->instr);

            nir_ssa_def_rewrite_uses_after(ssa, nir_src_for_ssa(&mul->dest.dest.ssa), &mul->instr);
         }
      }
   }
}

static void etna_lower_alu(nir_shader *shader, struct etna_compile *c)
{
   nir_foreach_function(function, shader) {
      if (function->impl)
         etna_lower_alu_impl(function->impl, c);
   }
}

static void
emit_inst(struct etna_compile *c, struct etna_inst *inst)
{
   c->code[c->inst_ptr++] = *inst;
}

/* to map nir srcs should to etna_inst srcs */
enum {
   SRC_0_1_2 = (0 << 0) | (1 << 2) | (2 << 4),
   SRC_0_1_X = (0 << 0) | (1 << 2) | (3 << 4),
   SRC_0_X_X = (0 << 0) | (3 << 2) | (3 << 4),
   SRC_0_X_1 = (0 << 0) | (3 << 2) | (1 << 4),
   SRC_0_1_0 = (0 << 0) | (1 << 2) | (0 << 4),
   SRC_X_X_0 = (3 << 0) | (3 << 2) | (0 << 4),
   SRC_0_X_0 = (0 << 0) | (3 << 2) | (0 << 4),
};

/* info to translate a nir op to etna_inst */
struct etna_op_info {
   uint8_t opcode; /* INST_OPCODE_ */
   uint8_t src; /* SRC_ enum  */
   uint8_t cond; /* INST_CONDITION_ */
   uint8_t type; /* INST_TYPE_ */
};

static const struct etna_op_info etna_ops[] = {
   [0 ... nir_num_opcodes - 1] = {0xff},
#undef TRUE
#undef FALSE
#define OPCT(nir, op, src, cond, type) [nir_op_##nir] = { \
   INST_OPCODE_##op, \
   SRC_##src, \
   INST_CONDITION_##cond, \
   INST_TYPE_##type \
}
#define OPC(nir, op, src, cond) OPCT(nir, op, src, cond, F32)
#define IOPC(nir, op, src, cond) OPCT(nir, op, src, cond, S32)
#define UOPC(nir, op, src, cond) OPCT(nir, op, src, cond, U32)
#define OP(nir, op, src) OPC(nir, op, src, TRUE)
#define IOP(nir, op, src) IOPC(nir, op, src, TRUE)
#define UOP(nir, op, src) UOPC(nir, op, src, TRUE)
   OP(mov, MOV, X_X_0), OP(fneg, MOV, X_X_0), OP(fabs, MOV, X_X_0), OP(fsat, MOV, X_X_0),
   OP(fmul, MUL, 0_1_X), OP(fadd, ADD, 0_X_1), OP(ffma, MAD, 0_1_2),
   OP(fdot2, DP2, 0_1_X), OP(fdot3, DP3, 0_1_X), OP(fdot4, DP4, 0_1_X),
   OPC(fmin, SELECT, 0_1_0, GT), OPC(fmax, SELECT, 0_1_0, LT),
   OP(ffract, FRC, X_X_0), OP(frcp, RCP, X_X_0), OP(frsq, RSQ, X_X_0),
   OP(fsqrt, SQRT, X_X_0), OP(fsin, SIN, X_X_0), OP(fcos, COS, X_X_0),
   OP(fsign, SIGN, X_X_0), OP(ffloor, FLOOR, X_X_0), OP(fceil, CEIL, X_X_0),
   OP(flog2, LOG, X_X_0), OP(fexp2, EXP, X_X_0),
   OPC(seq, SET, 0_1_X, EQ), OPC(sne, SET, 0_1_X, NE), OPC(sge, SET, 0_1_X, GE), OPC(slt, SET, 0_1_X, LT),
   OPC(fcsel, SELECT, 0_1_2, NZ),
   OP(fdiv, DIV, 0_1_X),
   OP(fddx, DSX, 0_X_0), OP(fddy, DSY, 0_X_0),

   /* type convert */
   IOP(i2f32, I2F, 0_X_X),
   UOP(u2f32, I2F, 0_X_X),
   IOP(f2i32, F2I, 0_X_X),
   UOP(f2u32, F2I, 0_X_X),
   UOP(b2f32, AND, 0_X_X), /* AND with fui(1.0f) */
   UOP(b2i32, AND, 0_X_X), /* AND with 1 */
   OPC(f2b32, CMP, 0_X_X, NE), /* != 0.0 */
   UOPC(i2b32, CMP, 0_X_X, NE), /* != 0 */

   /* arithmetic */
   IOP(iadd, ADD, 0_X_1),
   IOP(imul, IMULLO0, 0_1_X),
   /* IOP(imad, IMADLO0, 0_1_2), */
   IOP(ineg, ADD, X_X_0), /* ADD 0, -x */
   IOP(iabs, IABS, X_X_0),
   IOP(isign, SIGN, X_X_0),
   IOPC(imin, SELECT, 0_1_0, GT),
   IOPC(imax, SELECT, 0_1_0, LT),
   UOPC(umin, SELECT, 0_1_0, GT),
   UOPC(umax, SELECT, 0_1_0, LT),

   /* select */
   UOPC(b32csel, SELECT, 0_1_2, NZ),

   /* compare with int result */
    OPC(feq32, CMP, 0_1_X, EQ),
    OPC(fne32, CMP, 0_1_X, NE),
    OPC(fge32, CMP, 0_1_X, GE),
    OPC(flt32, CMP, 0_1_X, LT),
   IOPC(ieq32, CMP, 0_1_X, EQ),
   IOPC(ine32, CMP, 0_1_X, NE),
   IOPC(ige32, CMP, 0_1_X, GE),
   IOPC(ilt32, CMP, 0_1_X, LT),
   UOPC(uge32, CMP, 0_1_X, GE),
   UOPC(ult32, CMP, 0_1_X, LT),

   /* bit ops */
   IOP(ior,  OR,  0_X_1),
   IOP(iand, AND, 0_X_1),
   IOP(ixor, XOR, 0_X_1),
   IOP(inot, NOT, X_X_0),
   IOP(ishl, LSHIFT, 0_X_1),
   IOP(ishr, RSHIFT, 0_X_1),
   UOP(ushr, RSHIFT, 0_X_1),
};

static void
etna_emit_block_start(struct etna_compile *c, unsigned block)
{
   c->block_ptr[block] = c->inst_ptr;
}

static void
etna_emit_alu(struct etna_compile *c, nir_op op, struct etna_inst_dst dst,
              struct etna_inst_src src[3], bool saturate)
{
   struct etna_op_info ei = etna_ops[op];
   unsigned swiz_scalar = INST_SWIZ_BROADCAST(ffs(dst.write_mask) - 1);

   assert(ei.opcode != 0xff);

   struct etna_inst inst = {
      .opcode = ei.opcode,
      .type = ei.type,
      .cond = ei.cond,
      .dst = dst,
      .sat = saturate,
   };

   switch (op) {
   case nir_op_fdiv:
   case nir_op_flog2:
   case nir_op_fsin:
   case nir_op_fcos:
      if (c->specs->has_new_transcendentals)
         inst.tex.amode = 1;
      /* fall through */
   case nir_op_frsq:
   case nir_op_frcp:
   case nir_op_fexp2:
   case nir_op_fsqrt:
   case nir_op_imul:
      /* scalar instructions we want src to be in x component */
      src[0].swiz = inst_swiz_compose(src[0].swiz, swiz_scalar);
      src[1].swiz = inst_swiz_compose(src[1].swiz, swiz_scalar);
      break;
   /* deal with instructions which don't have 1:1 mapping */
   case nir_op_b2f32:
      inst.src[2] = etna_immediate_float(1.0f);
      break;
   case nir_op_b2i32:
      inst.src[2] = etna_immediate_int(1);
      break;
   case nir_op_f2b32:
      inst.src[1] = etna_immediate_float(0.0f);
      break;
   case nir_op_i2b32:
      inst.src[1] = etna_immediate_int(0);
      break;
   case nir_op_ineg:
      inst.src[0] = etna_immediate_int(0);
      src[0].neg = 1;
      break;
   default:
      break;
   }

   /* set the "true" value for CMP instructions */
   if (inst.opcode == INST_OPCODE_CMP)
      inst.src[2] = etna_immediate_int(-1);

   for (unsigned j = 0; j < 3; j++) {
      unsigned i = ((ei.src >> j*2) & 3);
      if (i < 3)
         inst.src[j] = src[i];
   }

   emit_inst(c, &inst);
}

static void
etna_emit_tex(struct etna_compile *c, nir_texop op, unsigned texid, unsigned dst_swiz,
              struct etna_inst_dst dst, struct etna_inst_src coord,
              struct etna_inst_src lod_bias, struct etna_inst_src compare)
{
   struct etna_inst inst = {
      .dst = dst,
      .tex.id = texid + (is_fs(c) ? 0 : c->specs->vertex_sampler_offset),
      .tex.swiz = dst_swiz,
      .src[0] = coord,
   };

   if (lod_bias.use)
      inst.src[1] = lod_bias;

   if (compare.use)
      inst.src[2] = compare;

   switch (op) {
   case nir_texop_tex: inst.opcode = INST_OPCODE_TEXLD; break;
   case nir_texop_txb: inst.opcode = INST_OPCODE_TEXLDB; break;
   case nir_texop_txl: inst.opcode = INST_OPCODE_TEXLDL; break;
   default:
      assert(0);
   }

   emit_inst(c, &inst);
}

static void
etna_emit_jump(struct etna_compile *c, unsigned block, struct etna_inst_src condition)
{
   if (!condition.use) {
      emit_inst(c, &(struct etna_inst) {.opcode = INST_OPCODE_BRANCH, .imm = block });
      return;
   }

   struct etna_inst inst = {
      .opcode = INST_OPCODE_BRANCH,
      .cond = INST_CONDITION_NOT,
      .type = INST_TYPE_U32,
      .src[0] = condition,
      .imm = block,
   };
   inst.src[0].swiz = INST_SWIZ_BROADCAST(inst.src[0].swiz & 3);
   emit_inst(c, &inst);
}

static void
etna_emit_discard(struct etna_compile *c, struct etna_inst_src condition)
{
   if (!condition.use) {
      emit_inst(c, &(struct etna_inst) { .opcode = INST_OPCODE_TEXKILL });
      return;
   }

   struct etna_inst inst = {
      .opcode = INST_OPCODE_TEXKILL,
      .cond = INST_CONDITION_NZ,
      .type = (c->specs->halti < 2) ? INST_TYPE_F32 : INST_TYPE_U32,
      .src[0] = condition,
   };
   inst.src[0].swiz = INST_SWIZ_BROADCAST(inst.src[0].swiz & 3);
   emit_inst(c, &inst);
}

static void
etna_emit_output(struct etna_compile *c, nir_variable *var, struct etna_inst_src src)
{
   struct etna_shader_io_file *sf = &c->variant->outfile;

   if (is_fs(c)) {
      switch (var->data.location) {
      case FRAG_RESULT_COLOR:
      case FRAG_RESULT_DATA0: /* DATA0 is used by gallium shaders for color */
         c->variant->ps_color_out_reg = src.reg;
         break;
      case FRAG_RESULT_DEPTH:
         c->variant->ps_depth_out_reg = src.reg;
         break;
      default:
         unreachable("Unsupported fs output");
      }
      return;
   }

   switch (var->data.location) {
   case VARYING_SLOT_POS:
      c->variant->vs_pos_out_reg = src.reg;
      break;
   case VARYING_SLOT_PSIZ:
      c->variant->vs_pointsize_out_reg = src.reg;
      break;
   default:
      sf->reg[sf->num_reg].reg = src.reg;
      sf->reg[sf->num_reg].slot = var->data.location;
      sf->reg[sf->num_reg].num_components = glsl_get_components(var->type);
      sf->num_reg++;
      break;
   }
}

static void
etna_emit_load_ubo(struct etna_compile *c, struct etna_inst_dst dst,
                   struct etna_inst_src src, struct etna_inst_src base)
{
   /* convert float offset back to integer */
   if (c->specs->halti < 2) {
      emit_inst(c, &(struct etna_inst) {
         .opcode = INST_OPCODE_F2I,
         .type = INST_TYPE_U32,
         .dst = dst,
         .src[0] = src,
      });

      emit_inst(c, &(struct etna_inst) {
         .opcode = INST_OPCODE_LOAD,
         .type = INST_TYPE_U32,
         .dst = dst,
         .src[0] = {
            .use = 1,
            .rgroup = INST_RGROUP_TEMP,
            .reg = dst.reg,
            .swiz = INST_SWIZ_BROADCAST(ffs(dst.write_mask) - 1)
         },
         .src[1] = base,
      });

      return;
   }

   emit_inst(c, &(struct etna_inst) {
      .opcode = INST_OPCODE_LOAD,
      .type = INST_TYPE_U32,
      .dst = dst,
      .src[0] = src,
      .src[1] = base,
   });
}

#define OPT(nir, pass, ...) ({                             \
   bool this_progress = false;                             \
   NIR_PASS(this_progress, nir, pass, ##__VA_ARGS__);      \
   this_progress;                                          \
})
#define OPT_V(nir, pass, ...) NIR_PASS_V(nir, pass, ##__VA_ARGS__)

static void
etna_optimize_loop(nir_shader *s)
{
   bool progress;
   do {
      progress = false;

      OPT_V(s, nir_lower_vars_to_ssa);
      progress |= OPT(s, nir_opt_copy_prop_vars);
      progress |= OPT(s, nir_copy_prop);
      progress |= OPT(s, nir_opt_dce);
      progress |= OPT(s, nir_opt_cse);
      progress |= OPT(s, nir_opt_peephole_select, 16, true, true);
      progress |= OPT(s, nir_opt_intrinsics);
      progress |= OPT(s, nir_opt_algebraic);
      progress |= OPT(s, nir_opt_constant_folding);
      progress |= OPT(s, nir_opt_dead_cf);
      if (OPT(s, nir_opt_trivial_continues)) {
         progress = true;
         /* If nir_opt_trivial_continues makes progress, then we need to clean
          * things up if we want any hope of nir_opt_if or nir_opt_loop_unroll
          * to make progress.
          */
         OPT(s, nir_copy_prop);
         OPT(s, nir_opt_dce);
      }
      progress |= OPT(s, nir_opt_loop_unroll, nir_var_all);
      progress |= OPT(s, nir_opt_if, false);
      progress |= OPT(s, nir_opt_remove_phis);
      progress |= OPT(s, nir_opt_undef);
   }
   while (progress);
}

static int
etna_glsl_type_size(const struct glsl_type *type, bool bindless)
{
   return glsl_count_attribute_slots(type, false);
}

static void
copy_uniform_state_to_shader(struct etna_shader_variant *sobj, uint64_t *consts, unsigned count)
{
   struct etna_shader_uniform_info *uinfo = &sobj->uniforms;

   uinfo->imm_count = count * 4;
   uinfo->imm_data = MALLOC(uinfo->imm_count * sizeof(*uinfo->imm_data));
   uinfo->imm_contents = MALLOC(uinfo->imm_count * sizeof(*uinfo->imm_contents));

   for (unsigned i = 0; i < uinfo->imm_count; i++) {
      uinfo->imm_data[i] = consts[i];
      uinfo->imm_contents[i] = consts[i] >> 32;
   }

   etna_set_shader_uniforms_dirty_flags(sobj);
}

#include "etnaviv_compiler_nir_emit.h"

bool
etna_compile_shader_nir(struct etna_shader_variant *v)
{
   if (unlikely(!v))
      return false;

   struct etna_compile *c = CALLOC_STRUCT(etna_compile);
   if (!c)
      return false;

   c->variant = v;
   c->specs = v->shader->specs;
   c->nir = nir_shader_clone(NULL, v->shader->nir);

   nir_shader *s = c->nir;
   const struct etna_specs *specs = c->specs;

   v->stage = s->info.stage;
   v->num_loops = 0; /* TODO */
   v->vs_id_in_reg = -1;
   v->vs_pos_out_reg = -1;
   v->vs_pointsize_out_reg = -1;
   v->ps_color_out_reg = 0; /* 0 for shader that doesn't write fragcolor.. */
   v->ps_depth_out_reg = -1;

   /* setup input linking */
   struct etna_shader_io_file *sf = &v->infile;
   if (s->info.stage == MESA_SHADER_VERTEX) {
      nir_foreach_variable(var, &s->inputs) {
         unsigned idx = var->data.driver_location;
         sf->reg[idx].reg = idx;
         sf->reg[idx].slot = var->data.location;
         sf->reg[idx].num_components = glsl_get_components(var->type);
         sf->num_reg = MAX2(sf->num_reg, idx+1);
      }
   } else {
      unsigned count = 0;
      nir_foreach_variable(var, &s->inputs) {
         unsigned idx = var->data.driver_location;
         sf->reg[idx].reg = idx + 1;
         sf->reg[idx].slot = var->data.location;
         sf->reg[idx].num_components = glsl_get_components(var->type);
         sf->num_reg = MAX2(sf->num_reg, idx+1);
         count++;
      }
      assert(sf->num_reg == count);
   }

   NIR_PASS_V(s, nir_lower_io, ~nir_var_shader_out, etna_glsl_type_size,
            (nir_lower_io_options)0);

   OPT_V(s, nir_lower_regs_to_ssa);
   OPT_V(s, nir_lower_vars_to_ssa);
   OPT_V(s, nir_lower_indirect_derefs, nir_var_all);
   OPT_V(s, nir_lower_tex, &(struct nir_lower_tex_options) { .lower_txp = ~0u });
   OPT_V(s, nir_lower_alu_to_scalar, etna_alu_to_scalar_filter_cb, specs);

   etna_optimize_loop(s);

   OPT_V(s, etna_lower_io, v);

   /* lower pre-halti2 to float (halti0 has integers, but only scalar..) */
   if (c->specs->halti < 2) {
      /* use opt_algebraic between int_to_float and boot_to_float because
       * int_to_float emits ftrunc, and ftrunc lowering generates bool ops
       */
      OPT_V(s, nir_lower_int_to_float);
      OPT_V(s, nir_opt_algebraic);
      OPT_V(s, nir_lower_bool_to_float);
   } else {
      OPT_V(s, nir_lower_idiv, nir_lower_idiv_fast);
      OPT_V(s, nir_lower_bool_to_int32);
   }

   etna_optimize_loop(s);

   if (DBG_ENABLED(ETNA_DBG_DUMP_SHADERS))
      nir_print_shader(s, stdout);

   while( OPT(s, nir_opt_vectorize) );
   OPT_V(s, nir_lower_alu_to_scalar, etna_alu_to_scalar_filter_cb, specs);

   NIR_PASS_V(s, nir_remove_dead_variables, nir_var_function_temp);
   NIR_PASS_V(s, nir_opt_algebraic_late);

   NIR_PASS_V(s, nir_move_vec_src_uses_to_dest);
   NIR_PASS_V(s, nir_copy_prop);
   /* only HW supported integer source mod is ineg for iadd instruction (?) */
   NIR_PASS_V(s, nir_lower_to_source_mods, ~nir_lower_int_source_mods);
   /* need copy prop after uses_to_dest, and before src mods: see
    * dEQP-GLES2.functional.shaders.random.all_features.fragment.95
    */

   NIR_PASS_V(s, nir_opt_dce);

   NIR_PASS_V(s, etna_lower_alu, c);

   if (DBG_ENABLED(ETNA_DBG_DUMP_SHADERS))
      nir_print_shader(s, stdout);

   unsigned block_ptr[nir_shader_get_entrypoint(s)->num_blocks];
   c->block_ptr = block_ptr;

   unsigned num_consts;
   ASSERTED bool ok = emit_shader(c, &v->num_temps, &num_consts);
   assert(ok);

   /* empty shader, emit NOP */
   if (!c->inst_ptr)
      emit_inst(c, &(struct etna_inst) { .opcode = INST_OPCODE_NOP });

   /* assemble instructions, fixing up labels */
   uint32_t *code = MALLOC(c->inst_ptr * 16);
   for (unsigned i = 0; i < c->inst_ptr; i++) {
      struct etna_inst *inst = &c->code[i];
      if (inst->opcode == INST_OPCODE_BRANCH)
         inst->imm = block_ptr[inst->imm];

      inst->halti5 = specs->halti >= 5;
      etna_assemble(&code[i * 4], inst);
   }

   v->code_size = c->inst_ptr * 4;
   v->code = code;
   v->needs_icache = c->inst_ptr > specs->max_instructions;

   copy_uniform_state_to_shader(v, c->consts, num_consts);

   if (s->info.stage == MESA_SHADER_FRAGMENT) {
      v->input_count_unk8 = 31; /* XXX what is this */
      assert(v->ps_depth_out_reg <= 0);
      ralloc_free(c->nir);
      FREE(c);
      return true;
   }

   v->input_count_unk8 = DIV_ROUND_UP(v->infile.num_reg + 4, 16); /* XXX what is this */

   /* fill in "mystery meat" load balancing value. This value determines how
    * work is scheduled between VS and PS
    * in the unified shader architecture. More precisely, it is determined from
    * the number of VS outputs, as well as chip-specific
    * vertex output buffer size, vertex cache size, and the number of shader
    * cores.
    *
    * XXX this is a conservative estimate, the "optimal" value is only known for
    * sure at link time because some
    * outputs may be unused and thus unmapped. Then again, in the general use
    * case with GLSL the vertex and fragment
    * shaders are linked already before submitting to Gallium, thus all outputs
    * are used.
    *
    * note: TGSI compiler counts all outputs (including position and pointsize), here
    * v->outfile.num_reg only counts varyings, +1 to compensate for the position output
    * TODO: might have a problem that we don't count pointsize when it is used
    */

   int half_out = v->outfile.num_reg / 2 + 1;
   assert(half_out);

   uint32_t b = ((20480 / (specs->vertex_output_buffer_size -
                           2 * half_out * specs->vertex_cache_size)) +
                 9) /
                10;
   uint32_t a = (b + 256 / (specs->shader_core_count * half_out)) / 2;
   v->vs_load_balancing = VIVS_VS_LOAD_BALANCING_A(MIN2(a, 255)) |
                             VIVS_VS_LOAD_BALANCING_B(MIN2(b, 255)) |
                             VIVS_VS_LOAD_BALANCING_C(0x3f) |
                             VIVS_VS_LOAD_BALANCING_D(0x0f);

   ralloc_free(c->nir);
   FREE(c);
   return true;
}

void
etna_destroy_shader_nir(struct etna_shader_variant *shader)
{
   assert(shader);

   FREE(shader->code);
   FREE(shader->uniforms.imm_data);
   FREE(shader->uniforms.imm_contents);
   FREE(shader);
}

extern const char *tgsi_swizzle_names[];
void
etna_dump_shader_nir(const struct etna_shader_variant *shader)
{
   if (shader->stage == MESA_SHADER_VERTEX)
      printf("VERT\n");
   else
      printf("FRAG\n");

   etna_disasm(shader->code, shader->code_size, PRINT_RAW);

   printf("num loops: %i\n", shader->num_loops);
   printf("num temps: %i\n", shader->num_temps);
   printf("immediates:\n");
   for (int idx = 0; idx < shader->uniforms.imm_count; ++idx) {
      printf(" [%i].%s = %f (0x%08x) (%d)\n",
             idx / 4,
             tgsi_swizzle_names[idx % 4],
             *((float *)&shader->uniforms.imm_data[idx]),
             shader->uniforms.imm_data[idx],
             shader->uniforms.imm_contents[idx]);
   }
   printf("inputs:\n");
   for (int idx = 0; idx < shader->infile.num_reg; ++idx) {
      printf(" [%i] name=%s comps=%i\n", shader->infile.reg[idx].reg,
               (shader->stage == MESA_SHADER_VERTEX) ?
               gl_vert_attrib_name(shader->infile.reg[idx].slot) :
               gl_varying_slot_name(shader->infile.reg[idx].slot),
               shader->infile.reg[idx].num_components);
   }
   printf("outputs:\n");
   for (int idx = 0; idx < shader->outfile.num_reg; ++idx) {
      printf(" [%i] name=%s comps=%i\n", shader->outfile.reg[idx].reg,
               (shader->stage == MESA_SHADER_VERTEX) ?
               gl_varying_slot_name(shader->outfile.reg[idx].slot) :
               gl_frag_result_name(shader->outfile.reg[idx].slot),
               shader->outfile.reg[idx].num_components);
   }
   printf("special:\n");
   if (shader->stage == MESA_SHADER_VERTEX) {
      printf("  vs_pos_out_reg=%i\n", shader->vs_pos_out_reg);
      printf("  vs_pointsize_out_reg=%i\n", shader->vs_pointsize_out_reg);
      printf("  vs_load_balancing=0x%08x\n", shader->vs_load_balancing);
   } else {
      printf("  ps_color_out_reg=%i\n", shader->ps_color_out_reg);
      printf("  ps_depth_out_reg=%i\n", shader->ps_depth_out_reg);
   }
   printf("  input_count_unk8=0x%08x\n", shader->input_count_unk8);
}

static const struct etna_shader_inout *
etna_shader_vs_lookup(const struct etna_shader_variant *sobj,
                      const struct etna_shader_inout *in)
{
   for (int i = 0; i < sobj->outfile.num_reg; i++)
      if (sobj->outfile.reg[i].slot == in->slot)
         return &sobj->outfile.reg[i];

   return NULL;
}

bool
etna_link_shader_nir(struct etna_shader_link_info *info,
                     const struct etna_shader_variant *vs,
                     const struct etna_shader_variant *fs)
{
   int comp_ofs = 0;
   /* For each fragment input we need to find the associated vertex shader
    * output, which can be found by matching on semantic name and index. A
    * binary search could be used because the vs outputs are sorted by their
    * semantic index and grouped by semantic type by fill_in_vs_outputs.
    */
   assert(fs->infile.num_reg < ETNA_NUM_INPUTS);
   info->pcoord_varying_comp_ofs = -1;

   for (int idx = 0; idx < fs->infile.num_reg; ++idx) {
      const struct etna_shader_inout *fsio = &fs->infile.reg[idx];
      const struct etna_shader_inout *vsio = etna_shader_vs_lookup(vs, fsio);
      struct etna_varying *varying;
      bool interpolate_always = true;

      assert(fsio->reg > 0 && fsio->reg <= ARRAY_SIZE(info->varyings));

      if (fsio->reg > info->num_varyings)
         info->num_varyings = fsio->reg;

      varying = &info->varyings[fsio->reg - 1];
      varying->num_components = fsio->num_components;

      if (!interpolate_always) /* colors affected by flat shading */
         varying->pa_attributes = 0x200;
      else /* texture coord or other bypasses flat shading */
         varying->pa_attributes = 0x2f1;

      varying->use[0] = VARYING_COMPONENT_USE_UNUSED;
      varying->use[1] = VARYING_COMPONENT_USE_UNUSED;
      varying->use[2] = VARYING_COMPONENT_USE_UNUSED;
      varying->use[3] = VARYING_COMPONENT_USE_UNUSED;

      /* point coord is an input to the PS without matching VS output,
       * so it gets a varying slot without being assigned a VS register.
       */
      if (fsio->slot == VARYING_SLOT_PNTC) {
         varying->use[0] = VARYING_COMPONENT_USE_POINTCOORD_X;
         varying->use[1] = VARYING_COMPONENT_USE_POINTCOORD_Y;

         info->pcoord_varying_comp_ofs = comp_ofs;
      } else {
         if (vsio == NULL) { /* not found -- link error */
            BUG("Semantic value not found in vertex shader outputs\n");
            return true;
         }
         varying->reg = vsio->reg;
      }

      comp_ofs += varying->num_components;
   }

   assert(info->num_varyings == fs->infile.num_reg);

   return false;
}
