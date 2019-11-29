/*
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
 */

#include "etnaviv_asm.h"
#include "etnaviv_context.h"

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_worklist.h"
#include "util/register_allocate.h"

#define ALU_SWIZ(s) INST_SWIZ((s)->swizzle[0], (s)->swizzle[1], (s)->swizzle[2], (s)->swizzle[3])
#define SRC_DISABLE ((hw_src){})
#define SRC_CONST(idx, s) ((hw_src){.use=1, .rgroup = INST_RGROUP_UNIFORM_0, .reg=idx, .swiz=s})
#define SRC_REG(idx, s) ((hw_src){.use=1, .rgroup = INST_RGROUP_TEMP, .reg=idx, .swiz=s})

#define emit(type, args...) etna_emit_##type(state->c, args)

typedef struct etna_inst_dst hw_dst;
typedef struct etna_inst_src hw_src;

enum {
   BYPASS_DST = 1,
   BYPASS_SRC = 2,
};

struct state {
   struct etna_compile *c;

   unsigned const_count;

   nir_shader *shader;
   nir_function_impl *impl;

   /* ra state */
   struct ra_graph *g;
   struct ra_regs *regs;
   unsigned *live_map;
   unsigned num_nodes;
};

#define compile_error(ctx, args...) ({ \
   printf(args); \
   ctx->error = true; \
   assert(0); \
})

static inline hw_src
src_swizzle(hw_src src, unsigned swizzle)
{
   if (src.rgroup != INST_RGROUP_IMMEDIATE)
      src.swiz = inst_swiz_compose(src.swiz, swizzle);

   return src;
}

static inline bool is_sysval(nir_instr *instr)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   return intr->intrinsic == nir_intrinsic_load_front_face ||
          intr->intrinsic == nir_intrinsic_load_frag_coord;
}

/* constants are represented as 64-bit ints
 * 32-bit for the value and 32-bit for the type (imm, uniform, etc)
 */

#define CONST_VAL(a, b) (nir_const_value) {.u64 = (uint64_t)(a) << 32 | (uint64_t)(b)}
#define CONST(x) CONST_VAL(ETNA_IMMEDIATE_CONSTANT, x)
#define UNIFORM(x) CONST_VAL(ETNA_IMMEDIATE_UNIFORM, x)
#define UNIFORM_BASE(x) CONST_VAL(ETNA_IMMEDIATE_UBO0_ADDR, x)
#define TEXSCALE(x, i) CONST_VAL(ETNA_IMMEDIATE_TEXRECT_SCALE_X + (i), x)

static int
const_add(uint64_t *c, uint64_t value)
{
   for (unsigned i = 0; i < 4; i++) {
      if (c[i] == value || !c[i]) {
         c[i] = value;
         return i;
      }
   }
   return -1;
}

static hw_src
const_src(struct state *state, nir_const_value *value, unsigned num_components)
{
   /* use inline immediates if possible */
   if (state->c->specs->halti >= 2 && num_components == 1 &&
       value[0].u64 >> 32 == ETNA_IMMEDIATE_CONSTANT) {
      uint32_t bits = value[0].u32;

      /* "float" - shifted by 12 */
      if ((bits & 0xfff) == 0)
         return etna_immediate_src(0, bits >> 12);

      /* "unsigned" - raw 20 bit value */
      if (bits < (1 << 20))
         return etna_immediate_src(2, bits);

      /* "signed" - sign extended 20-bit (sign included) value */
      if (bits >= 0xfff80000)
         return etna_immediate_src(1, bits);
   }

   unsigned i;
   int swiz = -1;
   for (i = 0; swiz < 0; i++) {
      uint64_t *a = &state->c->consts[i*4];
      uint64_t save[4];
      memcpy(save, a, sizeof(save));
      swiz = 0;
      for (unsigned j = 0; j < num_components; j++) {
         int c = const_add(a, value[j].u64);
         if (c < 0) {
            memcpy(a, save, sizeof(save));
            swiz = -1;
            break;
         }
         swiz |= c << j * 2;
      }
   }

   assert(i <= ETNA_MAX_IMM / 4);
   state->const_count = MAX2(state->const_count, i);

   return SRC_CONST(i - 1, swiz);
}

struct ssa_reg {
   uint8_t idx;
   uint8_t src_swizzle;
   uint8_t dst_swizzle;
   uint8_t write_mask;
};

/* Swizzles and write masks can be used to layer virtual non-interfering
 * registers on top of the real VEC4 registers. For example, the virtual
 * VEC3_XYZ register and the virtual SCALAR_W register that use the same
 * physical VEC4 base register do not interfere.
 */
enum {
   REG_CLASS_VIRT_SCALAR,
   REG_CLASS_VIRT_VEC2,
   REG_CLASS_VIRT_VEC3,
   REG_CLASS_VEC4,
   /* special vec2 class for fast transcendentals, limited to XY or ZW */
   REG_CLASS_VIRT_VEC2T,
   /* special classes for LOAD - contiguous components */
   REG_CLASS_VIRT_VEC2C,
   REG_CLASS_VIRT_VEC3C,
   NUM_REG_CLASSES,
} reg_class;

enum {
   REG_TYPE_VEC4,
   REG_TYPE_VIRT_VEC3_XYZ,
   REG_TYPE_VIRT_VEC3_XYW,
   REG_TYPE_VIRT_VEC3_XZW,
   REG_TYPE_VIRT_VEC3_YZW,
   REG_TYPE_VIRT_VEC2_XY,
   REG_TYPE_VIRT_VEC2_XZ,
   REG_TYPE_VIRT_VEC2_XW,
   REG_TYPE_VIRT_VEC2_YZ,
   REG_TYPE_VIRT_VEC2_YW,
   REG_TYPE_VIRT_VEC2_ZW,
   REG_TYPE_VIRT_SCALAR_X,
   REG_TYPE_VIRT_SCALAR_Y,
   REG_TYPE_VIRT_SCALAR_Z,
   REG_TYPE_VIRT_SCALAR_W,
   REG_TYPE_VIRT_VEC2T_XY,
   REG_TYPE_VIRT_VEC2T_ZW,
   REG_TYPE_VIRT_VEC2C_XY,
   REG_TYPE_VIRT_VEC2C_YZ,
   REG_TYPE_VIRT_VEC2C_ZW,
   REG_TYPE_VIRT_VEC3C_XYZ,
   REG_TYPE_VIRT_VEC3C_YZW,
   NUM_REG_TYPES,
} reg_type;

/* writemask when used as dest */
static const uint8_t
reg_writemask[NUM_REG_TYPES] = {
   [REG_TYPE_VEC4] = 0xf,
   [REG_TYPE_VIRT_SCALAR_X] = 0x1,
   [REG_TYPE_VIRT_SCALAR_Y] = 0x2,
   [REG_TYPE_VIRT_VEC2_XY] = 0x3,
   [REG_TYPE_VIRT_VEC2T_XY] = 0x3,
   [REG_TYPE_VIRT_VEC2C_XY] = 0x3,
   [REG_TYPE_VIRT_SCALAR_Z] = 0x4,
   [REG_TYPE_VIRT_VEC2_XZ] = 0x5,
   [REG_TYPE_VIRT_VEC2_YZ] = 0x6,
   [REG_TYPE_VIRT_VEC2C_YZ] = 0x6,
   [REG_TYPE_VIRT_VEC3_XYZ] = 0x7,
   [REG_TYPE_VIRT_VEC3C_XYZ] = 0x7,
   [REG_TYPE_VIRT_SCALAR_W] = 0x8,
   [REG_TYPE_VIRT_VEC2_XW] = 0x9,
   [REG_TYPE_VIRT_VEC2_YW] = 0xa,
   [REG_TYPE_VIRT_VEC3_XYW] = 0xb,
   [REG_TYPE_VIRT_VEC2_ZW] = 0xc,
   [REG_TYPE_VIRT_VEC2T_ZW] = 0xc,
   [REG_TYPE_VIRT_VEC2C_ZW] = 0xc,
   [REG_TYPE_VIRT_VEC3_XZW] = 0xd,
   [REG_TYPE_VIRT_VEC3_YZW] = 0xe,
   [REG_TYPE_VIRT_VEC3C_YZW] = 0xe,
};

/* how to swizzle when used as a src */
static const uint8_t
reg_swiz[NUM_REG_TYPES] = {
   [REG_TYPE_VEC4] = INST_SWIZ_IDENTITY,
   [REG_TYPE_VIRT_SCALAR_X] = INST_SWIZ_IDENTITY,
   [REG_TYPE_VIRT_SCALAR_Y] = SWIZZLE(Y, Y, Y, Y),
   [REG_TYPE_VIRT_VEC2_XY] = INST_SWIZ_IDENTITY,
   [REG_TYPE_VIRT_VEC2T_XY] = INST_SWIZ_IDENTITY,
   [REG_TYPE_VIRT_VEC2C_XY] = INST_SWIZ_IDENTITY,
   [REG_TYPE_VIRT_SCALAR_Z] = SWIZZLE(Z, Z, Z, Z),
   [REG_TYPE_VIRT_VEC2_XZ] = SWIZZLE(X, Z, X, Z),
   [REG_TYPE_VIRT_VEC2_YZ] = SWIZZLE(Y, Z, Y, Z),
   [REG_TYPE_VIRT_VEC2C_YZ] = SWIZZLE(Y, Z, Y, Z),
   [REG_TYPE_VIRT_VEC3_XYZ] = INST_SWIZ_IDENTITY,
   [REG_TYPE_VIRT_VEC3C_XYZ] = INST_SWIZ_IDENTITY,
   [REG_TYPE_VIRT_SCALAR_W] = SWIZZLE(W, W, W, W),
   [REG_TYPE_VIRT_VEC2_XW] = SWIZZLE(X, W, X, W),
   [REG_TYPE_VIRT_VEC2_YW] = SWIZZLE(Y, W, Y, W),
   [REG_TYPE_VIRT_VEC3_XYW] = SWIZZLE(X, Y, W, X),
   [REG_TYPE_VIRT_VEC2_ZW] = SWIZZLE(Z, W, Z, W),
   [REG_TYPE_VIRT_VEC2T_ZW] = SWIZZLE(Z, W, Z, W),
   [REG_TYPE_VIRT_VEC2C_ZW] = SWIZZLE(Z, W, Z, W),
   [REG_TYPE_VIRT_VEC3_XZW] = SWIZZLE(X, Z, W, X),
   [REG_TYPE_VIRT_VEC3_YZW] = SWIZZLE(Y, Z, W, X),
   [REG_TYPE_VIRT_VEC3C_YZW] = SWIZZLE(Y, Z, W, X),
};

/* how to swizzle when used as a dest */
static const uint8_t
reg_dst_swiz[NUM_REG_TYPES] = {
   [REG_TYPE_VEC4] = INST_SWIZ_IDENTITY,
   [REG_TYPE_VIRT_SCALAR_X] = INST_SWIZ_IDENTITY,
   [REG_TYPE_VIRT_SCALAR_Y] = SWIZZLE(X, X, X, X),
   [REG_TYPE_VIRT_VEC2_XY] = INST_SWIZ_IDENTITY,
   [REG_TYPE_VIRT_VEC2T_XY] = INST_SWIZ_IDENTITY,
   [REG_TYPE_VIRT_VEC2C_XY] = INST_SWIZ_IDENTITY,
   [REG_TYPE_VIRT_SCALAR_Z] = SWIZZLE(X, X, X, X),
   [REG_TYPE_VIRT_VEC2_XZ] = SWIZZLE(X, X, Y, Y),
   [REG_TYPE_VIRT_VEC2_YZ] = SWIZZLE(X, X, Y, Y),
   [REG_TYPE_VIRT_VEC2C_YZ] = SWIZZLE(X, X, Y, Y),
   [REG_TYPE_VIRT_VEC3_XYZ] = INST_SWIZ_IDENTITY,
   [REG_TYPE_VIRT_VEC3C_XYZ] = INST_SWIZ_IDENTITY,
   [REG_TYPE_VIRT_SCALAR_W] = SWIZZLE(X, X, X, X),
   [REG_TYPE_VIRT_VEC2_XW] = SWIZZLE(X, X, Y, Y),
   [REG_TYPE_VIRT_VEC2_YW] = SWIZZLE(X, X, Y, Y),
   [REG_TYPE_VIRT_VEC3_XYW] = SWIZZLE(X, Y, Z, Z),
   [REG_TYPE_VIRT_VEC2_ZW] = SWIZZLE(X, X, X, Y),
   [REG_TYPE_VIRT_VEC2T_ZW] = SWIZZLE(X, X, X, Y),
   [REG_TYPE_VIRT_VEC2C_ZW] = SWIZZLE(X, X, X, Y),
   [REG_TYPE_VIRT_VEC3_XZW] = SWIZZLE(X, Y, Y, Z),
   [REG_TYPE_VIRT_VEC3_YZW] = SWIZZLE(X, X, Y, Z),
   [REG_TYPE_VIRT_VEC3C_YZW] = SWIZZLE(X, X, Y, Z),
};

static inline int reg_get_type(int virt_reg)
{
   return virt_reg % NUM_REG_TYPES;
}

static inline int reg_get_base(struct state *state, int virt_reg)
{
   /* offset by 1 to avoid reserved position register */
   if (state->shader->info.stage == MESA_SHADER_FRAGMENT)
      return (virt_reg / NUM_REG_TYPES + 1) % ETNA_MAX_TEMPS;
   return virt_reg / NUM_REG_TYPES;
}

/* use "r63.z" for depth reg, it will wrap around to r0.z by reg_get_base
 * (fs registers are offset by 1 to avoid reserving r0)
 */
#define REG_FRAG_DEPTH ((ETNA_MAX_TEMPS - 1) * NUM_REG_TYPES + REG_TYPE_VIRT_SCALAR_Z)

static inline int reg_get_class(int virt_reg)
{
   switch (reg_get_type(virt_reg)) {
   case REG_TYPE_VEC4:
      return REG_CLASS_VEC4;
   case REG_TYPE_VIRT_VEC3_XYZ:
   case REG_TYPE_VIRT_VEC3_XYW:
   case REG_TYPE_VIRT_VEC3_XZW:
   case REG_TYPE_VIRT_VEC3_YZW:
      return REG_CLASS_VIRT_VEC3;
   case REG_TYPE_VIRT_VEC2_XY:
   case REG_TYPE_VIRT_VEC2_XZ:
   case REG_TYPE_VIRT_VEC2_XW:
   case REG_TYPE_VIRT_VEC2_YZ:
   case REG_TYPE_VIRT_VEC2_YW:
   case REG_TYPE_VIRT_VEC2_ZW:
      return REG_CLASS_VIRT_VEC2;
   case REG_TYPE_VIRT_SCALAR_X:
   case REG_TYPE_VIRT_SCALAR_Y:
   case REG_TYPE_VIRT_SCALAR_Z:
   case REG_TYPE_VIRT_SCALAR_W:
      return REG_CLASS_VIRT_SCALAR;
   case REG_TYPE_VIRT_VEC2T_XY:
   case REG_TYPE_VIRT_VEC2T_ZW:
      return REG_CLASS_VIRT_VEC2T;
   case REG_TYPE_VIRT_VEC2C_XY:
   case REG_TYPE_VIRT_VEC2C_YZ:
   case REG_TYPE_VIRT_VEC2C_ZW:
      return REG_CLASS_VIRT_VEC2C;
   case REG_TYPE_VIRT_VEC3C_XYZ:
   case REG_TYPE_VIRT_VEC3C_YZW:
      return REG_CLASS_VIRT_VEC3C;
   }

   assert(false);
   return 0;
}

/* get unique ssa/reg index for nir_src */
static unsigned
src_index(nir_function_impl *impl, nir_src *src)
{
   return src->is_ssa ? src->ssa->index : (src->reg.reg->index + impl->ssa_alloc);
}

/* get unique ssa/reg index for nir_dest */
static unsigned
dest_index(nir_function_impl *impl, nir_dest *dest)
{
   return dest->is_ssa ? dest->ssa.index : (dest->reg.reg->index + impl->ssa_alloc);
}

/* nir_src to allocated register */
static hw_src
ra_src(struct state *state, nir_src *src)
{
   unsigned reg = ra_get_node_reg(state->g, state->live_map[src_index(state->impl, src)]);
   return SRC_REG(reg_get_base(state, reg), reg_swiz[reg_get_type(reg)]);
}

static hw_src
get_src(struct state *state, nir_src *src)
{
   if (!src->is_ssa)
      return ra_src(state, src);

   nir_instr *instr = src->ssa->parent_instr;

   if (instr->pass_flags & BYPASS_SRC) {
      assert(instr->type == nir_instr_type_alu);
      nir_alu_instr *alu = nir_instr_as_alu(instr);
      assert(alu->op == nir_op_mov);
      return src_swizzle(get_src(state, &alu->src[0].src), ALU_SWIZ(&alu->src[0]));
   }

   switch (instr->type) {
   case nir_instr_type_load_const:
      return const_src(state, nir_instr_as_load_const(instr)->value, src->ssa->num_components);
   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
      switch (intr->intrinsic) {
      case nir_intrinsic_load_input:
      case nir_intrinsic_load_instance_id:
      case nir_intrinsic_load_uniform:
         return ra_src(state, src);
      case nir_intrinsic_load_front_face:
         return (hw_src) { .use = 1, .rgroup = INST_RGROUP_INTERNAL };
      case nir_intrinsic_load_frag_coord:
         return SRC_REG(0, INST_SWIZ_IDENTITY);
      default:
         compile_error(state->c, "Unhandled NIR intrinsic type: %s\n",
                       nir_intrinsic_infos[intr->intrinsic].name);
         break;
      }
   } break;
   case nir_instr_type_alu:
   case nir_instr_type_tex:
      return ra_src(state, src);
   case nir_instr_type_ssa_undef: {
      /* return zero to deal with broken Blur demo */
      nir_const_value value = CONST(0);
      return src_swizzle(const_src(state, &value, 1), SWIZZLE(X,X,X,X));
   }
   default:
      compile_error(state->c, "Unhandled NIR instruction type: %d\n", instr->type);
      break;
   }

   return SRC_DISABLE;
}

static void
update_swiz_mask(nir_alu_instr *alu, nir_dest *dest, unsigned *swiz, unsigned *mask)
{
   if (!swiz)
      return;

   bool is_vec = dest != NULL;
   unsigned swizzle = 0, write_mask = 0;
   for (unsigned i = 0; i < 4; i++) {
      /* channel not written */
      if (!(alu->dest.write_mask & (1 << i)))
         continue;
      /* src is different (only check for vecN) */
      if (is_vec && alu->src[i].src.ssa != &dest->ssa)
         continue;

      unsigned src_swiz = is_vec ? alu->src[i].swizzle[0] : alu->src[0].swizzle[i];
      swizzle |= (*swiz >> src_swiz * 2 & 3) << i * 2;
      /* this channel isn't written through this chain */
      if (*mask & (1 << src_swiz))
         write_mask |= 1 << i;
   }
   *swiz = swizzle;
   *mask = write_mask;
}

static bool
vec_dest_has_swizzle(nir_alu_instr *vec, nir_ssa_def *ssa)
{
   for (unsigned i = 0; i < 4; i++) {
      if (!(vec->dest.write_mask & (1 << i)) || vec->src[i].src.ssa != ssa)
         continue;

      if (vec->src[i].swizzle[0] != i)
         return true;
   }

   /* don't deal with possible bypassed vec/mov chain */
   nir_foreach_use(use_src, ssa) {
      nir_instr *instr = use_src->parent_instr;
      if (instr->type != nir_instr_type_alu)
         continue;

      nir_alu_instr *alu = nir_instr_as_alu(instr);

      switch (alu->op) {
      case nir_op_mov:
      case nir_op_vec2:
      case nir_op_vec3:
      case nir_op_vec4:
         return true;
      default:
         break;
      }
   }
   return false;
}

static nir_dest *
real_dest(nir_dest *dest, unsigned *swiz, unsigned *mask)
{
   if (!dest || !dest->is_ssa)
      return dest;

   bool can_bypass_src = !list_length(&dest->ssa.if_uses);
   nir_instr *p_instr = dest->ssa.parent_instr;

   /* if used by a vecN, the "real" destination becomes the vecN destination
    * lower_alu guarantees that values used by a vecN are only used by that vecN
    * we can apply the same logic to movs in a some cases too
    */
   nir_foreach_use(use_src, &dest->ssa) {
      nir_instr *instr = use_src->parent_instr;

      /* src bypass check: for now only deal with tex src mov case
       * note: for alu don't bypass mov for multiple uniform sources
       */
      switch (instr->type) {
      case nir_instr_type_tex:
         if (p_instr->type == nir_instr_type_alu &&
             nir_instr_as_alu(p_instr)->op == nir_op_mov) {
            break;
         }
      default:
         can_bypass_src = false;
         break;
      }

      if (instr->type != nir_instr_type_alu)
         continue;

      nir_alu_instr *alu = nir_instr_as_alu(instr);

      switch (alu->op) {
      case nir_op_vec2:
      case nir_op_vec3:
      case nir_op_vec4:
         assert(list_length(&dest->ssa.if_uses) == 0);
         nir_foreach_use(use_src, &dest->ssa)
            assert(use_src->parent_instr == instr);

         update_swiz_mask(alu, dest, swiz, mask);
         break;
      case nir_op_mov: {
         switch (dest->ssa.parent_instr->type) {
         case nir_instr_type_alu:
         case nir_instr_type_tex:
            break;
         default:
            continue;
         }
         if (list_length(&dest->ssa.if_uses) || list_length(&dest->ssa.uses) > 1)
            continue;

         update_swiz_mask(alu, NULL, swiz, mask);
         break;
      };
      default:
         continue;
      }

      assert(!(instr->pass_flags & BYPASS_SRC));
      instr->pass_flags |= BYPASS_DST;
      return real_dest(&alu->dest.dest, swiz, mask);
   }

   if (can_bypass_src && !(p_instr->pass_flags & BYPASS_DST)) {
      p_instr->pass_flags |= BYPASS_SRC;
      return NULL;
   }

   return dest;
}

/* get allocated dest register for nir_dest
 * *p_swiz tells how the components need to be placed into register
 */
static hw_dst
ra_dest(struct state *state, nir_dest *dest, unsigned *p_swiz)
{
   unsigned swiz = INST_SWIZ_IDENTITY, mask = 0xf;
   dest = real_dest(dest, &swiz, &mask);

   unsigned r = ra_get_node_reg(state->g, state->live_map[dest_index(state->impl, dest)]);
   unsigned t = reg_get_type(r);

   *p_swiz = inst_swiz_compose(swiz, reg_dst_swiz[t]);

   return (hw_dst) {
      .use = 1,
      .reg = reg_get_base(state, r),
      .write_mask = inst_write_mask_compose(mask, reg_writemask[t]),
   };
}

/* if instruction dest needs a register, return nir_dest for it */
static nir_dest *
dest_for_instr(nir_instr *instr)
{
   nir_dest *dest = NULL;

   switch (instr->type) {
   case nir_instr_type_alu:
      dest = &nir_instr_as_alu(instr)->dest.dest;
      break;
   case nir_instr_type_tex:
      dest = &nir_instr_as_tex(instr)->dest;
      break;
   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
      if (intr->intrinsic == nir_intrinsic_load_uniform ||
          intr->intrinsic == nir_intrinsic_load_input ||
          intr->intrinsic == nir_intrinsic_load_instance_id)
         dest = &intr->dest;
   } break;
   case nir_instr_type_deref:
      return NULL;
   default:
      break;
   }
   return real_dest(dest, NULL, NULL);
}

struct live_def {
   nir_instr *instr;
   nir_dest *dest; /* cached dest_for_instr */
   unsigned live_start, live_end; /* live range */
};

static void
range_include(struct live_def *def, unsigned index)
{
   if (def->live_start > index)
      def->live_start = index;
   if (def->live_end < index)
      def->live_end = index;
}

struct live_defs_state {
   unsigned num_defs;
   unsigned bitset_words;

   nir_function_impl *impl;
   nir_block *block; /* current block pointer */
   unsigned index; /* current live index */

   struct live_def *defs;
   unsigned *live_map; /* to map ssa/reg index into defs array */

   nir_block_worklist worklist;
};

static bool
init_liveness_block(nir_block *block,
                    struct live_defs_state *state)
{
   block->live_in = reralloc(block, block->live_in, BITSET_WORD,
                             state->bitset_words);
   memset(block->live_in, 0, state->bitset_words * sizeof(BITSET_WORD));

   block->live_out = reralloc(block, block->live_out, BITSET_WORD,
                              state->bitset_words);
   memset(block->live_out, 0, state->bitset_words * sizeof(BITSET_WORD));

   nir_block_worklist_push_head(&state->worklist, block);

   return true;
}

static bool
set_src_live(nir_src *src, void *void_state)
{
   struct live_defs_state *state = void_state;

   if (src->is_ssa) {
      nir_instr *instr = src->ssa->parent_instr;

      if (is_sysval(instr) || instr->type == nir_instr_type_deref)
         return true;

      switch (instr->type) {
      case nir_instr_type_load_const:
      case nir_instr_type_ssa_undef:
         return true;
      case nir_instr_type_alu: {
         /* alu op bypass */
         nir_alu_instr *alu = nir_instr_as_alu(instr);
         if (instr->pass_flags & BYPASS_SRC) {
            for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; i++)
               set_src_live(&alu->src[i].src, state);
            return true;
         }
      } break;
      default:
         break;
      }
   }

   unsigned i = state->live_map[src_index(state->impl, src)];
   assert(i != ~0u);

   BITSET_SET(state->block->live_in, i);
   range_include(&state->defs[i], state->index);

   return true;
}

static bool
propagate_across_edge(nir_block *pred, nir_block *succ,
                      struct live_defs_state *state)
{
   BITSET_WORD progress = 0;
   for (unsigned i = 0; i < state->bitset_words; ++i) {
      progress |= succ->live_in[i] & ~pred->live_out[i];
      pred->live_out[i] |= succ->live_in[i];
   }
   return progress != 0;
}

static unsigned
live_defs(nir_function_impl *impl, struct live_def *defs, unsigned *live_map)
{
   struct live_defs_state state;
   unsigned block_live_index[impl->num_blocks + 1];

   state.impl = impl;
   state.defs = defs;
   state.live_map = live_map;

   state.num_defs = 0;
   nir_foreach_block(block, impl) {
      block_live_index[block->index] = state.num_defs;
      nir_foreach_instr(instr, block) {
         nir_dest *dest = dest_for_instr(instr);
         if (!dest)
            continue;

         unsigned idx = dest_index(impl, dest);
         /* register is already in defs */
         if (live_map[idx] != ~0u)
            continue;

         defs[state.num_defs] = (struct live_def) {instr, dest, state.num_defs, 0};

         /* input live from the start */
         if (instr->type == nir_instr_type_intrinsic) {
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            if (intr->intrinsic == nir_intrinsic_load_input ||
                intr->intrinsic == nir_intrinsic_load_instance_id)
               defs[state.num_defs].live_start = 0;
         }

         live_map[idx] = state.num_defs;
         state.num_defs++;
      }
   }
   block_live_index[impl->num_blocks] = state.num_defs;

   nir_block_worklist_init(&state.worklist, impl->num_blocks, NULL);

   /* We now know how many unique ssa definitions we have and we can go
    * ahead and allocate live_in and live_out sets and add all of the
    * blocks to the worklist.
    */
   state.bitset_words = BITSET_WORDS(state.num_defs);
   nir_foreach_block(block, impl) {
      init_liveness_block(block, &state);
   }

   /* We're now ready to work through the worklist and update the liveness
    * sets of each of the blocks.  By the time we get to this point, every
    * block in the function implementation has been pushed onto the
    * worklist in reverse order.  As long as we keep the worklist
    * up-to-date as we go, everything will get covered.
    */
   while (!nir_block_worklist_is_empty(&state.worklist)) {
      /* We pop them off in the reverse order we pushed them on.  This way
       * the first walk of the instructions is backwards so we only walk
       * once in the case of no control flow.
       */
      nir_block *block = nir_block_worklist_pop_head(&state.worklist);
      state.block = block;

      memcpy(block->live_in, block->live_out,
             state.bitset_words * sizeof(BITSET_WORD));

      state.index = block_live_index[block->index + 1];

      nir_if *following_if = nir_block_get_following_if(block);
      if (following_if)
         set_src_live(&following_if->condition, &state);

      nir_foreach_instr_reverse(instr, block) {
         /* when we come across the next "live" instruction, decrement index */
         if (state.index && instr == defs[state.index - 1].instr) {
            state.index--;
            /* the only source of writes to registers is phis:
             * we don't expect any partial write_mask alus
             * so clearing live_in here is OK
             */
            BITSET_CLEAR(block->live_in, state.index);
         }

         /* don't set_src_live for not-emitted instructions */
         if (instr->pass_flags)
            continue;

         unsigned index = state.index;

         /* output live till the end */
         if (instr->type == nir_instr_type_intrinsic) {
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            if (intr->intrinsic == nir_intrinsic_store_deref)
               state.index = ~0u;
         }

         nir_foreach_src(instr, set_src_live, &state);

         state.index = index;
      }
      assert(state.index == block_live_index[block->index]);

      /* Walk over all of the predecessors of the current block updating
       * their live in with the live out of this one.  If anything has
       * changed, add the predecessor to the work list so that we ensure
       * that the new information is used.
       */
      set_foreach(block->predecessors, entry) {
         nir_block *pred = (nir_block *)entry->key;
         if (propagate_across_edge(pred, block, &state))
            nir_block_worklist_push_tail(&state.worklist, pred);
      }
   }

   nir_block_worklist_fini(&state.worklist);

   /* apply live_in/live_out to ranges */

   nir_foreach_block(block, impl) {
      BITSET_WORD tmp;
      int i;

      BITSET_FOREACH_SET(i, tmp, block->live_in, state.num_defs)
         range_include(&state.defs[i], block_live_index[block->index]);

      BITSET_FOREACH_SET(i, tmp, block->live_out, state.num_defs)
         range_include(&state.defs[i], block_live_index[block->index + 1]);
   }

   return state.num_defs;
}

/* precomputed by register_allocate  */
static unsigned int *q_values[] = {
   (unsigned int[]) {1, 2, 3, 4, 2, 2, 3, },
   (unsigned int[]) {3, 5, 6, 6, 5, 5, 6, },
   (unsigned int[]) {3, 4, 4, 4, 4, 4, 4, },
   (unsigned int[]) {1, 1, 1, 1, 1, 1, 1, },
   (unsigned int[]) {1, 2, 2, 2, 1, 2, 2, },
   (unsigned int[]) {2, 3, 3, 3, 2, 3, 3, },
   (unsigned int[]) {2, 2, 2, 2, 2, 2, 2, },
};

static void
ra_assign(struct state *state, nir_shader *shader)
{
   struct ra_regs *regs = ra_alloc_reg_set(NULL, ETNA_MAX_TEMPS *
                  NUM_REG_TYPES, false);

   /* classes always be created from index 0, so equal to the class enum
    * which represents a register with (c+1) components
    */
   for (int c = 0; c < NUM_REG_CLASSES; c++)
      ra_alloc_reg_class(regs);
   /* add each register of each class */
   for (int r = 0; r < NUM_REG_TYPES * ETNA_MAX_TEMPS; r++)
      ra_class_add_reg(regs, reg_get_class(r), r);
   /* set conflicts */
   for (int r = 0; r < ETNA_MAX_TEMPS; r++) {
      for (int i = 0; i < NUM_REG_TYPES; i++) {
         for (int j = 0; j < i; j++) {
            if (reg_writemask[i] & reg_writemask[j]) {
               ra_add_reg_conflict(regs, NUM_REG_TYPES * r + i,
                                         NUM_REG_TYPES * r + j);
            }
         }
      }
   }
   ra_set_finalize(regs, q_values);

   nir_function_impl *impl = nir_shader_get_entrypoint(shader);

   /* liveness and interference */

   nir_index_blocks(impl);
   nir_index_ssa_defs(impl);
   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block)
         instr->pass_flags = 0;
   }

   /* this gives an approximation/upper limit on how many nodes are needed
    * (some ssa values do not represent an allocated register)
    */
   unsigned max_nodes = impl->ssa_alloc + impl->reg_alloc;
   unsigned *live_map = ralloc_array(NULL, unsigned, max_nodes);
   memset(live_map, 0xff, sizeof(unsigned) * max_nodes);
   struct live_def *defs = rzalloc_array(NULL, struct live_def, max_nodes);

   unsigned num_nodes = live_defs(impl, defs, live_map);
   struct ra_graph *g = ra_alloc_interference_graph(regs, num_nodes);

   /* set classes from num_components */
   for (unsigned i = 0; i < num_nodes; i++) {
      nir_instr *instr = defs[i].instr;
      nir_dest *dest = defs[i].dest;
      unsigned c = nir_dest_num_components(*dest) - 1;

      if (instr->type == nir_instr_type_alu &&
          state->c->specs->has_new_transcendentals) {
         switch (nir_instr_as_alu(instr)->op) {
         case nir_op_fdiv:
         case nir_op_flog2:
         case nir_op_fsin:
         case nir_op_fcos:
            assert(dest->is_ssa);
            c = REG_CLASS_VIRT_VEC2T;
         default:
            break;
         }
      }

      if (instr->type == nir_instr_type_intrinsic) {
         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
         if (intr->intrinsic == nir_intrinsic_load_uniform) {
            /* make sure there isn't any reswizzling */
            assert(dest == &intr->dest);
            if (dest->ssa.num_components == 2)
               c = REG_CLASS_VIRT_VEC2C;
            if (dest->ssa.num_components == 3)
               c = REG_CLASS_VIRT_VEC3C;
         }
      }

      ra_set_node_class(g, i, c);
   }

   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_dest *dest = dest_for_instr(instr);
         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
         unsigned reg;

         switch (intr->intrinsic) {
         case nir_intrinsic_store_deref: {
            /* don't want outputs to be swizzled
             * TODO: better would be to set the type to X/XY/XYZ/XYZW
             * TODO: what if fragcoord.z is read after writing fragdepth?
             */
            nir_deref_instr *deref = nir_src_as_deref(intr->src[0]);
            unsigned index = live_map[src_index(impl, &intr->src[1])];

            if (shader->info.stage == MESA_SHADER_FRAGMENT &&
                deref->var->data.location == FRAG_RESULT_DEPTH) {
               ra_set_node_reg(g, index, REG_FRAG_DEPTH);
            } else {
               ra_set_node_class(g, index, REG_CLASS_VEC4);
            }
         } continue;
         case nir_intrinsic_load_input:
            reg = nir_intrinsic_base(intr) * NUM_REG_TYPES + (unsigned[]) {
               REG_TYPE_VIRT_SCALAR_X,
               REG_TYPE_VIRT_VEC2_XY,
               REG_TYPE_VIRT_VEC3_XYZ,
               REG_TYPE_VEC4,
            }[nir_dest_num_components(*dest) - 1];
            break;
         case nir_intrinsic_load_instance_id:
            reg = state->c->variant->infile.num_reg * NUM_REG_TYPES + REG_TYPE_VIRT_SCALAR_Y;
            break;
         default:
            continue;
         }

         ra_set_node_reg(g, live_map[dest_index(impl, dest)], reg);
      }
   }

   /* add interference for intersecting live ranges */
   for (unsigned i = 0; i < num_nodes; i++) {
      assert(defs[i].live_start < defs[i].live_end);
      for (unsigned j = 0; j < i; j++) {
         if (defs[i].live_start >= defs[j].live_end || defs[j].live_start >= defs[i].live_end)
            continue;
         ra_add_node_interference(g, i, j);
      }
   }

   ralloc_free(defs);

   /* Allocate registers */
   ASSERTED bool ok = ra_allocate(g);
   assert(ok);

   state->g = g;
   state->regs = regs;
   state->live_map = live_map;
   state->num_nodes = num_nodes;
}

static unsigned
ra_finish(struct state *state)
{
   /* TODO: better way to get number of registers used? */
   unsigned j = 0;
   for (unsigned i = 0; i < state->num_nodes; i++) {
      j = MAX2(j, reg_get_base(state, ra_get_node_reg(state->g, i)) + 1);
   }

   ralloc_free(state->g);
   ralloc_free(state->regs);
   ralloc_free(state->live_map);

   return j;
}

static void
emit_alu(struct state *state, nir_alu_instr * alu)
{
   const nir_op_info *info = &nir_op_infos[alu->op];

   /* marked as dead instruction (vecN and other bypassed instr) */
   if (alu->instr.pass_flags)
      return;

   assert(!(alu->op >= nir_op_vec2 && alu->op <= nir_op_vec4));

   unsigned dst_swiz;
   hw_dst dst = ra_dest(state, &alu->dest.dest, &dst_swiz);

   /* compose alu write_mask with RA write mask */
   if (!alu->dest.dest.is_ssa)
      dst.write_mask = inst_write_mask_compose(alu->dest.write_mask, dst.write_mask);

   switch (alu->op) {
   case nir_op_fdot2:
   case nir_op_fdot3:
   case nir_op_fdot4:
      /* not per-component - don't compose dst_swiz */
      dst_swiz = INST_SWIZ_IDENTITY;
      break;
   default:
      break;
   }

   hw_src srcs[3];

   for (int i = 0; i < info->num_inputs; i++) {
      nir_alu_src *asrc = &alu->src[i];
      hw_src src;

      src = src_swizzle(get_src(state, &asrc->src), ALU_SWIZ(asrc));
      src = src_swizzle(src, dst_swiz);

      if (src.rgroup != INST_RGROUP_IMMEDIATE) {
         src.neg = asrc->negate || (alu->op == nir_op_fneg);
         src.abs = asrc->abs || (alu->op == nir_op_fabs);
      } else {
         assert(!asrc->negate && alu->op != nir_op_fneg);
         assert(!asrc->abs && alu->op != nir_op_fabs);
      }

      srcs[i] = src;
   }

   emit(alu, alu->op, dst, srcs, alu->dest.saturate || (alu->op == nir_op_fsat));
}

static void
emit_tex(struct state *state, nir_tex_instr * tex)
{
   unsigned dst_swiz;
   hw_dst dst = ra_dest(state, &tex->dest, &dst_swiz);
   nir_src *coord = NULL, *lod_bias = NULL, *compare = NULL;

   for (unsigned i = 0; i < tex->num_srcs; i++) {
      switch (tex->src[i].src_type) {
      case nir_tex_src_coord:
         coord = &tex->src[i].src;
         break;
      case nir_tex_src_bias:
      case nir_tex_src_lod:
         assert(!lod_bias);
         lod_bias = &tex->src[i].src;
         break;
      case nir_tex_src_comparator:
         compare = &tex->src[i].src;
         break;
      default:
         compile_error(state->c, "Unhandled NIR tex src type: %d\n",
                       tex->src[i].src_type);
         break;
      }
   }

   emit(tex, tex->op, tex->sampler_index, dst_swiz, dst, get_src(state, coord),
        lod_bias ? get_src(state, lod_bias) : SRC_DISABLE,
        compare ? get_src(state, compare) : SRC_DISABLE);
}

static void
emit_intrinsic(struct state *state, nir_intrinsic_instr * intr)
{
   switch (intr->intrinsic) {
   case nir_intrinsic_store_deref:
      emit(output, nir_src_as_deref(intr->src[0])->var, get_src(state, &intr->src[1]));
      break;
   case nir_intrinsic_discard_if:
      emit(discard, get_src(state, &intr->src[0]));
      break;
   case nir_intrinsic_discard:
      emit(discard, SRC_DISABLE);
      break;
   case nir_intrinsic_load_uniform: {
      unsigned dst_swiz;
      hw_dst dst = ra_dest(state, &intr->dest, &dst_swiz);
      /* TODO: might have a problem with dst_swiz .. */
      emit(load_ubo, dst, get_src(state, &intr->src[0]), const_src(state, &UNIFORM_BASE(nir_intrinsic_base(intr) * 16), 1));
   } break;
   case nir_intrinsic_load_front_face:
   case nir_intrinsic_load_frag_coord:
      assert(intr->dest.is_ssa); /* TODO - lower phis could cause this */
      break;
   case nir_intrinsic_load_input:
   case nir_intrinsic_load_instance_id:
      break;
   default:
      compile_error(state->c, "Unhandled NIR intrinsic type: %s\n",
                    nir_intrinsic_infos[intr->intrinsic].name);
   }
}

static void
emit_instr(struct state *state, nir_instr * instr)
{
   switch (instr->type) {
   case nir_instr_type_alu:
      emit_alu(state, nir_instr_as_alu(instr));
      break;
   case nir_instr_type_tex:
      emit_tex(state, nir_instr_as_tex(instr));
      break;
   case nir_instr_type_intrinsic:
      emit_intrinsic(state, nir_instr_as_intrinsic(instr));
      break;
   case nir_instr_type_jump:
      assert(nir_instr_is_last(instr));
   case nir_instr_type_load_const:
   case nir_instr_type_ssa_undef:
   case nir_instr_type_deref:
      break;
   default:
      compile_error(state->c, "Unhandled NIR instruction type: %d\n", instr->type);
      break;
   }
}

static void
emit_block(struct state *state, nir_block * block)
{
   emit(block_start, block->index);

   nir_foreach_instr(instr, block)
      emit_instr(state, instr);

   /* succs->index < block->index is for the loop case  */
   nir_block *succs = block->successors[0];
   if (nir_block_ends_in_jump(block) || succs->index < block->index)
      emit(jump, succs->index, SRC_DISABLE);
}

static void
emit_cf_list(struct state *state, struct exec_list *list);

static void
emit_if(struct state *state, nir_if * nif)
{
   emit(jump, nir_if_first_else_block(nif)->index, get_src(state, &nif->condition));
   emit_cf_list(state, &nif->then_list);

   /* jump at end of then_list to skip else_list
    * not needed if then_list already ends with a jump or else_list is empty
    */
   if (!nir_block_ends_in_jump(nir_if_last_then_block(nif)) &&
       !nir_cf_list_is_empty_block(&nif->else_list))
      emit(jump, nir_if_last_else_block(nif)->successors[0]->index, SRC_DISABLE);

   emit_cf_list(state, &nif->else_list);
}

static void
emit_cf_list(struct state *state, struct exec_list *list)
{
   foreach_list_typed(nir_cf_node, node, node, list) {
      switch (node->type) {
      case nir_cf_node_block:
         emit_block(state, nir_cf_node_as_block(node));
         break;
      case nir_cf_node_if:
         emit_if(state, nir_cf_node_as_if(node));
         break;
      case nir_cf_node_loop:
         emit_cf_list(state, &nir_cf_node_as_loop(node)->body);
         break;
      default:
         compile_error(state->c, "Unknown NIR node type\n");
         break;
      }
   }
}

/* based on nir_lower_vec_to_movs */
static unsigned
insert_vec_mov(nir_alu_instr *vec, unsigned start_idx, nir_shader *shader)
{
   assert(start_idx < nir_op_infos[vec->op].num_inputs);
   unsigned write_mask = (1u << start_idx);

   nir_alu_instr *mov = nir_alu_instr_create(shader, nir_op_mov);
   nir_alu_src_copy(&mov->src[0], &vec->src[start_idx], mov);

   mov->src[0].swizzle[0] = vec->src[start_idx].swizzle[0];
   mov->src[0].negate = vec->src[start_idx].negate;
   mov->src[0].abs = vec->src[start_idx].abs;

   unsigned num_components = 1;

   for (unsigned i = start_idx + 1; i < 4; i++) {
      if (!(vec->dest.write_mask & (1 << i)))
         continue;

      if (nir_srcs_equal(vec->src[i].src, vec->src[start_idx].src) &&
          vec->src[i].negate == vec->src[start_idx].negate &&
          vec->src[i].abs == vec->src[start_idx].abs) {
         write_mask |= (1 << i);
         mov->src[0].swizzle[num_components] = vec->src[i].swizzle[0];
         num_components++;
      }
   }

   mov->dest.write_mask = (1 << num_components) - 1;
   nir_ssa_dest_init(&mov->instr, &mov->dest.dest, num_components, 32, NULL);

   /* replace vec srcs with inserted mov */
   for (unsigned i = 0, j = 0; i < 4; i++) {
      if (!(write_mask & (1 << i)))
         continue;

      nir_instr_rewrite_src(&vec->instr, &vec->src[i].src, nir_src_for_ssa(&mov->dest.dest.ssa));
      vec->src[i].swizzle[0] = j++;
   }

   nir_instr_insert_before(&vec->instr, &mov->instr);

   return write_mask;
}

/*
 * for vecN instructions:
 * -merge constant sources into a single src
 * -insert movs (nir_lower_vec_to_movs equivalent)
 * for non-vecN instructions:
 * -try to merge constants as single constant
 * -insert movs for multiple constants (pre-HALTI5)
 */
static void
lower_alu(struct state *state, nir_alu_instr *alu)
{
   const nir_op_info *info = &nir_op_infos[alu->op];

   nir_builder b;
   nir_builder_init(&b, state->impl);
   b.cursor = nir_before_instr(&alu->instr);

   switch (alu->op) {
   case nir_op_vec2:
   case nir_op_vec3:
   case nir_op_vec4:
      break;
   default:
      /* pre-GC7000L can only have 1 uniform src per instruction */
      if (state->c->specs->halti >= 5)
         return;

      nir_const_value value[4] = {};
      uint8_t swizzle[4][4] = {};
      unsigned swiz_max = 0, num_const = 0;

      for (unsigned i = 0; i < info->num_inputs; i++) {
         nir_const_value *cv = nir_src_as_const_value(alu->src[i].src);
         if (!cv)
            continue;

         unsigned num_components = info->input_sizes[i] ?: alu->dest.dest.ssa.num_components;
         for (unsigned j = 0; j < num_components; j++) {
            int idx = const_add(&value[0].u64, cv[alu->src[i].swizzle[j]].u64);
            swizzle[i][j] = idx;
            swiz_max = MAX2(swiz_max, (unsigned) idx);
         }
         num_const++;
      }

      /* nothing to do */
      if (num_const <= 1)
         return;

      /* resolve with single combined const src */
      if (swiz_max < 4) {
         nir_ssa_def *def = nir_build_imm(&b, swiz_max + 1, 32, value);

         for (unsigned i = 0; i < info->num_inputs; i++) {
            nir_const_value *cv = nir_src_as_const_value(alu->src[i].src);
            if (!cv)
               continue;

            nir_instr_rewrite_src(&alu->instr, &alu->src[i].src, nir_src_for_ssa(def));

            for (unsigned j = 0; j < 4; j++)
               alu->src[i].swizzle[j] = swizzle[i][j];
         }
         return;
      }

      /* resolve with movs */
      num_const = 0;
      for (unsigned i = 0; i < info->num_inputs; i++) {
         nir_const_value *cv = nir_src_as_const_value(alu->src[i].src);
         if (!cv)
            continue;

         num_const++;
         if (num_const == 1)
            continue;

         nir_ssa_def *mov = nir_mov(&b, alu->src[i].src.ssa);
         nir_instr_rewrite_src(&alu->instr, &alu->src[i].src, nir_src_for_ssa(mov));
      }
      return;
   }

   nir_const_value value[4];
   unsigned num_components = 0;

   for (unsigned i = 0; i < info->num_inputs; i++) {
      nir_const_value *cv = nir_src_as_const_value(alu->src[i].src);
      if (cv)
         value[num_components++] = cv[alu->src[i].swizzle[0]];
   }

   /* if there is more than one constant source to the vecN, combine them
    * into a single load_const (removing the vecN completely if all components
    * are constant)
    */
   if (num_components > 1) {
      nir_ssa_def *def = nir_build_imm(&b, num_components, 32, value);

      if (num_components == info->num_inputs) {
         nir_ssa_def_rewrite_uses(&alu->dest.dest.ssa, nir_src_for_ssa(def));
         nir_instr_remove(&alu->instr);
         return;
      }

      for (unsigned i = 0, j = 0; i < info->num_inputs; i++) {
         nir_const_value *cv = nir_src_as_const_value(alu->src[i].src);
         if (!cv)
            continue;

         nir_instr_rewrite_src(&alu->instr, &alu->src[i].src, nir_src_for_ssa(def));
         alu->src[i].swizzle[0] = j++;
      }
   }

   unsigned finished_write_mask = 0;
   for (unsigned i = 0; i < 4; i++) {
      if (!(alu->dest.write_mask & (1 << i)))
            continue;

      nir_ssa_def *ssa = alu->src[i].src.ssa;

      /* check that vecN instruction is only user of this */
      bool need_mov = list_length(&ssa->if_uses) != 0;
      nir_foreach_use(use_src, ssa) {
         if (use_src->parent_instr != &alu->instr)
            need_mov = true;
      }

      nir_instr *instr = ssa->parent_instr;
      switch (instr->type) {
      case nir_instr_type_alu:
      case nir_instr_type_tex:
         break;
      case nir_instr_type_intrinsic:
         if (nir_instr_as_intrinsic(instr)->intrinsic == nir_intrinsic_load_input) {
            need_mov = vec_dest_has_swizzle(alu, &nir_instr_as_intrinsic(instr)->dest.ssa);
            break;
         }
      default:
         need_mov = true;
      }

      if (need_mov && !(finished_write_mask & (1 << i)))
         finished_write_mask |= insert_vec_mov(alu, i, state->shader);
   }
}

static bool
emit_shader(struct etna_compile *c, unsigned *num_temps, unsigned *num_consts)
{
   nir_shader *shader = c->nir;

   struct state state = {
      .c = c,
      .shader = shader,
      .impl = nir_shader_get_entrypoint(shader),
   };

   nir_builder b;
   nir_builder_init(&b, state.impl);

   /* convert non-dynamic uniform loads to constants, etc */
   nir_foreach_block(block, state.impl) {
      nir_foreach_instr_safe(instr, block) {
         switch(instr->type) {
         case nir_instr_type_alu:
            /* deals with vecN and const srcs */
            lower_alu(&state, nir_instr_as_alu(instr));
            break;
         case nir_instr_type_load_const: {
            nir_load_const_instr *load_const = nir_instr_as_load_const(instr);
            for (unsigned  i = 0; i < load_const->def.num_components; i++)
               load_const->value[i] = CONST(load_const->value[i].u32);
         } break;
         case nir_instr_type_intrinsic: {
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            if (intr->intrinsic != nir_intrinsic_load_uniform)
               break;
            nir_const_value *off = nir_src_as_const_value(intr->src[0]);
            if (!off || off[0].u64 >> 32 != ETNA_IMMEDIATE_CONSTANT)
               break;

            unsigned base = nir_intrinsic_base(intr);
            /* pre halti2 uniform offset will be float */
            if (c->specs->halti < 2)
               base += (unsigned) off[0].f32 / 16;
            else
               base += off[0].u32 / 16;

            nir_const_value value[4];

            for (unsigned i = 0; i < intr->dest.ssa.num_components; i++) {
               if (nir_intrinsic_base(intr) < 0)
                  value[i] = TEXSCALE(~nir_intrinsic_base(intr), i);
               else
                  value[i] = UNIFORM(base * 4 + i);
            }

            b.cursor = nir_after_instr(instr);
            nir_ssa_def *def = nir_build_imm(&b, intr->dest.ssa.num_components, 32, value);

            nir_ssa_def_rewrite_uses(&intr->dest.ssa, nir_src_for_ssa(def));
            nir_instr_remove(instr);
         } break;
         default:
            break;
         }
      }
   }

   /* add mov for any store output using sysval/const  */
   nir_foreach_block(block, state.impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

         switch (intr->intrinsic) {
         case nir_intrinsic_store_deref: {
            nir_src *src = &intr->src[1];
            if (nir_src_is_const(*src) || is_sysval(src->ssa->parent_instr)) {
               b.cursor = nir_before_instr(instr);
               nir_instr_rewrite_src(instr, src, nir_src_for_ssa(nir_mov(&b, src->ssa)));
            }
         } break;
         default:
            break;
         }
      }
   }

   /* call directly to avoid validation (load_const don't pass validation at this point) */
   nir_convert_from_ssa(shader, true);
   nir_opt_dce(shader);

   ra_assign(&state, shader);

   emit_cf_list(&state, &nir_shader_get_entrypoint(shader)->body);

   *num_temps = ra_finish(&state);
   *num_consts = state.const_count;
   return true;
}
