/*
 * Copyright (c) 2017 Lima Project
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
 */

#include <string.h>

#include "util/ralloc.h"
#include "util/bitscan.h"
#include "compiler/nir/nir.h"

#include "ppir.h"

static void *ppir_node_create_ssa(ppir_block *block, ppir_op op, nir_ssa_def *ssa)
{
   ppir_node *node = ppir_node_create(block, op, ssa->index, 0);
   if (!node)
      return NULL;

   ppir_dest *dest = ppir_node_get_dest(node);
   dest->type = ppir_target_ssa;
   dest->ssa.num_components = ssa->num_components;
   dest->ssa.live_in = INT_MAX;
   dest->ssa.live_out = 0;
   dest->write_mask = u_bit_consecutive(0, ssa->num_components);

   if (node->type == ppir_node_type_load ||
       node->type == ppir_node_type_store)
      dest->ssa.is_head = true;

   return node;
}

static void *ppir_node_create_reg(ppir_block *block, ppir_op op,
                                  nir_reg_dest *reg, unsigned mask)
{
   ppir_node *node = ppir_node_create(block, op, reg->reg->index, mask);
   if (!node)
      return NULL;

   ppir_dest *dest = ppir_node_get_dest(node);

   list_for_each_entry(ppir_reg, r, &block->comp->reg_list, list) {
      if (r->index == reg->reg->index) {
         dest->reg = r;
         break;
      }
   }

   dest->type = ppir_target_register;
   dest->write_mask = mask;

   if (node->type == ppir_node_type_load ||
       node->type == ppir_node_type_store)
      dest->reg->is_head = true;

   return node;
}

static void *ppir_node_create_dest(ppir_block *block, ppir_op op,
                                   nir_dest *dest, unsigned mask)
{
   unsigned index = -1;

   if (dest) {
      if (dest->is_ssa)
         return ppir_node_create_ssa(block, op, &dest->ssa);
      else
         return ppir_node_create_reg(block, op, &dest->reg, mask);
   }

   return ppir_node_create(block, op, index, 0);
}

static void ppir_node_add_src(ppir_compiler *comp, ppir_node *node,
                              ppir_src *ps, nir_src *ns, unsigned mask)
{
   ppir_node *child = NULL;

   if (ns->is_ssa) {
      child = comp->var_nodes[ns->ssa->index];
      ppir_node_add_dep(node, child);
   }
   else {
      nir_register *reg = ns->reg.reg;
      while (mask) {
         int swizzle = ps->swizzle[u_bit_scan(&mask)];
         child = comp->var_nodes[(reg->index << 2) + comp->reg_base + swizzle];
         ppir_node_add_dep(node, child);
      }
   }

   ppir_dest *dest = ppir_node_get_dest(child);
   ppir_node_target_assign(ps, dest);
}

static int nir_to_ppir_opcodes[nir_num_opcodes] = {
   /* not supported */
   [0 ... nir_last_opcode] = -1,

   [nir_op_fmov] = ppir_op_mov,
   [nir_op_imov] = ppir_op_mov,
   [nir_op_fmul] = ppir_op_mul,
   [nir_op_fadd] = ppir_op_add,
   [nir_op_fdot2] = ppir_op_dot2,
   [nir_op_fdot3] = ppir_op_dot3,
   [nir_op_fdot4] = ppir_op_dot4,
   [nir_op_frsq] = ppir_op_rsqrt,
   [nir_op_flog2] = ppir_op_log2,
   [nir_op_fexp2] = ppir_op_exp2,
   [nir_op_fsqrt] = ppir_op_sqrt,
   [nir_op_fsin] = ppir_op_sin,
   [nir_op_fcos] = ppir_op_cos,
   [nir_op_fmax] = ppir_op_max,
   [nir_op_fmin] = ppir_op_min,
   [nir_op_frcp] = ppir_op_rcp,
   [nir_op_ffloor] = ppir_op_floor,
   [nir_op_fceil] = ppir_op_ceil,
   [nir_op_ffract] = ppir_op_fract,
   [nir_op_fand] = ppir_op_and,
   [nir_op_for] = ppir_op_or,
   [nir_op_fxor] = ppir_op_xor,
   [nir_op_sge] = ppir_op_ge,
   [nir_op_fge] = ppir_op_ge,
   [nir_op_slt] = ppir_op_lt,
   [nir_op_flt] = ppir_op_lt,
   [nir_op_seq] = ppir_op_eq,
   [nir_op_feq] = ppir_op_eq,
   [nir_op_sne] = ppir_op_ne,
   [nir_op_fne] = ppir_op_ne,
   [nir_op_fnot] = ppir_op_not,
   [nir_op_fcsel] = ppir_op_select,
   [nir_op_inot] = ppir_op_not,
   [nir_op_ftrunc] = ppir_op_trunc,
};

static ppir_node *ppir_emit_alu(ppir_block *block, nir_instr *ni)
{
   nir_alu_instr *instr = nir_instr_as_alu(ni);
   int op = nir_to_ppir_opcodes[instr->op];

   if (op < 0) {
      ppir_error("unsupported nir_op: %s\n", nir_op_infos[instr->op].name);
      return NULL;
   }

   ppir_alu_node *node = ppir_node_create_dest(block, op, &instr->dest.dest,
                                               instr->dest.write_mask);
   if (!node)
      return NULL;

   ppir_dest *pd = &node->dest;
   nir_alu_dest *nd = &instr->dest;
   if (nd->saturate)
      pd->modifier = ppir_outmod_clamp_fraction;

   unsigned src_mask;
   switch (op) {
   case ppir_op_dot2:
      src_mask = 0b0011;
      break;
   case ppir_op_dot3:
      src_mask = 0b0111;
      break;
   case ppir_op_dot4:
      src_mask = 0b1111;
      break;
   default:
      src_mask = pd->write_mask;
      break;
   }

   unsigned num_child = nir_op_infos[instr->op].num_inputs;
   node->num_src = num_child;

   for (int i = 0; i < num_child; i++) {
      nir_alu_src *ns = instr->src + i;
      ppir_src *ps = node->src + i;
      memcpy(ps->swizzle, ns->swizzle, sizeof(ps->swizzle));
      ppir_node_add_src(block->comp, &node->node, ps, &ns->src, src_mask);

      ps->absolute = ns->abs;
      ps->negate = ns->negate;
   }

   return &node->node;
}

static ppir_node *ppir_emit_intrinsic(ppir_block *block, nir_instr *ni)
{
   nir_intrinsic_instr *instr = nir_instr_as_intrinsic(ni);
   unsigned mask = 0;
   ppir_load_node *lnode;
   ppir_store_node *snode;

   switch (instr->intrinsic) {
   case nir_intrinsic_load_input:
      if (!instr->dest.is_ssa)
         mask = u_bit_consecutive(0, instr->num_components);

      lnode = ppir_node_create_dest(block, ppir_op_load_varying, &instr->dest, mask);
      if (!lnode)
         return NULL;

      lnode->num_components = instr->num_components;
      lnode->index = nir_intrinsic_base(instr) * 4 + nir_intrinsic_component(instr);
      return &lnode->node;

   case nir_intrinsic_load_frag_coord:
      if (!instr->dest.is_ssa)
         mask = u_bit_consecutive(0, instr->num_components);

      lnode = ppir_node_create_dest(block, ppir_op_load_fragcoord, &instr->dest, mask);
      if (!lnode)
         return NULL;

      lnode->num_components = instr->num_components;
      return &lnode->node;

   case nir_intrinsic_load_uniform:
      if (!instr->dest.is_ssa)
         mask = u_bit_consecutive(0, instr->num_components);

      lnode = ppir_node_create_dest(block, ppir_op_load_uniform, &instr->dest, mask);
      if (!lnode)
         return NULL;

      lnode->num_components = instr->num_components;
      lnode->index = nir_intrinsic_base(instr);
      lnode->index += (uint32_t)nir_src_as_float(instr->src[0]);

      return &lnode->node;

   case nir_intrinsic_store_output:
      snode = ppir_node_create_dest(block, ppir_op_store_color, NULL, 0);
      if (!snode)
         return NULL;

      snode->index = nir_intrinsic_base(instr);

      for (int i = 0; i < instr->num_components; i++)
         snode->src.swizzle[i] = i;

      ppir_node_add_src(block->comp, &snode->node, &snode->src, instr->src,
                        u_bit_consecutive(0, instr->num_components));

      return &snode->node;

   default:
      ppir_error("unsupported nir_intrinsic_instr %d\n", instr->intrinsic);
      return NULL;
   }
}

static ppir_node *ppir_emit_load_const(ppir_block *block, nir_instr *ni)
{
   nir_load_const_instr *instr = nir_instr_as_load_const(ni);
   ppir_const_node *node = ppir_node_create_ssa(block, ppir_op_const, &instr->def);
   if (!node)
      return NULL;

   assert(instr->def.bit_size == 32);

   for (int i = 0; i < instr->def.num_components; i++)
      node->constant.value[i].i = instr->value[i].i32;
   node->constant.num = instr->def.num_components;

   return &node->node;
}

static ppir_node *ppir_emit_ssa_undef(ppir_block *block, nir_instr *ni)
{
   ppir_error("nir_ssa_undef_instr not support\n");
   return NULL;
}

static ppir_node *ppir_emit_tex(ppir_block *block, nir_instr *ni)
{
   nir_tex_instr *instr = nir_instr_as_tex(ni);
   ppir_load_texture_node *node;

   if (instr->op != nir_texop_tex) {
      ppir_error("unsupported texop %d\n", instr->op);
      return NULL;
   }

   node = ppir_node_create_dest(block, ppir_op_load_texture, &instr->dest, 0);
   if (!node)
      return NULL;

   node->sampler = instr->texture_index;

   switch (instr->sampler_dim) {
   case GLSL_SAMPLER_DIM_2D:
   case GLSL_SAMPLER_DIM_RECT:
   case GLSL_SAMPLER_DIM_EXTERNAL:
      break;
   default:
      ppir_debug("unsupported sampler dim: %d\n", instr->sampler_dim);
      return NULL;
   }

   node->sampler_dim = instr->sampler_dim;

   for (int i = 0; i < instr->coord_components; i++)
         node->src_coords.swizzle[i] = i;

   assert(instr->num_srcs == 1);
   for (int i = 0; i < instr->num_srcs; i++) {
      switch (instr->src[i].src_type) {
      case nir_tex_src_coord:
         ppir_node_add_src(block->comp, &node->node, &node->src_coords, &instr->src[i].src,
                           u_bit_consecutive(0, instr->coord_components));
         break;
      default:
         ppir_debug("unknown texture source");
         return NULL;
      }
   }

   return &node->node;
}

static ppir_node *ppir_emit_jump(ppir_block *block, nir_instr *ni)
{
   ppir_error("nir_jump_instr not support\n");
   return NULL;
}

static ppir_node *(*ppir_emit_instr[nir_instr_type_phi])(ppir_block *, nir_instr *) = {
   [nir_instr_type_alu]        = ppir_emit_alu,
   [nir_instr_type_intrinsic]  = ppir_emit_intrinsic,
   [nir_instr_type_load_const] = ppir_emit_load_const,
   [nir_instr_type_ssa_undef]  = ppir_emit_ssa_undef,
   [nir_instr_type_tex]        = ppir_emit_tex,
   [nir_instr_type_jump]       = ppir_emit_jump,
};

static ppir_block *ppir_block_create(ppir_compiler *comp)
{
   ppir_block *block = rzalloc(comp, ppir_block);
   if (!block)
      return NULL;

   list_inithead(&block->node_list);
   list_inithead(&block->instr_list);

   return block;
}

static bool ppir_emit_block(ppir_compiler *comp, nir_block *nblock)
{
   ppir_block *block = ppir_block_create(comp);
   if (!block)
      return false;

   list_addtail(&block->list, &comp->block_list);
   block->comp = comp;

   nir_foreach_instr(instr, nblock) {
      assert(instr->type < nir_instr_type_phi);
      ppir_node *node = ppir_emit_instr[instr->type](block, instr);
      if (node)
         list_addtail(&node->list, &block->node_list);
   }

   return true;
}

static bool ppir_emit_if(ppir_compiler *comp, nir_if *nif)
{
   ppir_error("if nir_cf_node not support\n");
   return false;
}

static bool ppir_emit_loop(ppir_compiler *comp, nir_loop *nloop)
{
   ppir_error("loop nir_cf_node not support\n");
   return false;
}

static bool ppir_emit_function(ppir_compiler *comp, nir_function_impl *nfunc)
{
   ppir_error("function nir_cf_node not support\n");
   return false;
}

static bool ppir_emit_cf_list(ppir_compiler *comp, struct exec_list *list)
{
   foreach_list_typed(nir_cf_node, node, node, list) {
      bool ret;

      switch (node->type) {
      case nir_cf_node_block:
         ret = ppir_emit_block(comp, nir_cf_node_as_block(node));
         break;
      case nir_cf_node_if:
         ret = ppir_emit_if(comp, nir_cf_node_as_if(node));
         break;
      case nir_cf_node_loop:
         ret = ppir_emit_loop(comp, nir_cf_node_as_loop(node));
         break;
      case nir_cf_node_function:
         ret = ppir_emit_function(comp, nir_cf_node_as_function(node));
         break;
      default:
         ppir_error("unknown NIR node type %d\n", node->type);
         return false;
      }

      if (!ret)
         return false;
   }

   return true;
}

static ppir_compiler *ppir_compiler_create(void *prog, unsigned num_reg, unsigned num_ssa)
{
   ppir_compiler *comp = rzalloc_size(
      prog, sizeof(*comp) + ((num_reg << 2) + num_ssa) * sizeof(ppir_node *));
   if (!comp)
      return NULL;

   list_inithead(&comp->block_list);
   list_inithead(&comp->reg_list);

   comp->var_nodes = (ppir_node **)(comp + 1);
   comp->reg_base = num_ssa;
   comp->prog = prog;
   return comp;
}

bool ppir_compile_nir(struct lima_fs_shader_state *prog, struct nir_shader *nir,
                      struct ra_regs *ra)
{
   nir_function_impl *func = nir_shader_get_entrypoint(nir);
   ppir_compiler *comp = ppir_compiler_create(prog, func->reg_alloc, func->ssa_alloc);
   if (!comp)
      return false;

   comp->ra = ra;

   foreach_list_typed(nir_register, reg, node, &func->registers) {
      ppir_reg *r = rzalloc(comp, ppir_reg);
      if (!r)
         return false;

      r->index = reg->index;
      r->num_components = reg->num_components;
      r->live_in = INT_MAX;
      r->live_out = 0;
      r->is_head = false;
      list_addtail(&r->list, &comp->reg_list);
   }

   if (!ppir_emit_cf_list(comp, &func->body))
      goto err_out0;
   ppir_node_print_prog(comp);

   if (!ppir_lower_prog(comp))
      goto err_out0;

   if (!ppir_node_to_instr(comp))
      goto err_out0;

   if (!ppir_schedule_prog(comp))
      goto err_out0;

   if (!ppir_regalloc_prog(comp))
      goto err_out0;

   if (!ppir_codegen_prog(comp))
      goto err_out0;

   ralloc_free(comp);
   return true;

err_out0:
   ralloc_free(comp);
   return false;
}

