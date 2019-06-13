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

#include "util/ralloc.h"
#include "compiler/nir/nir.h"

#include "gpir.h"
#include "lima_context.h"


static inline void *gpir_node_create_ssa(gpir_block *block, gpir_op op, nir_ssa_def *ssa)
{
   int index = ssa->index;
   gpir_node *node = gpir_node_create(block, op);

   block->comp->var_nodes[index] = node;
   snprintf(node->name, sizeof(node->name), "ssa%d", index);
   list_addtail(&node->list, &block->node_list);
   return node;
}

static inline void *gpir_node_create_reg(gpir_block *block, gpir_op op, nir_reg_dest *reg)
{
   int index = reg->reg->index;
   gpir_node *node = gpir_node_create(block, op);
   gpir_store_node *store = gpir_node_create(block, gpir_op_store_reg);

   snprintf(node->name, sizeof(node->name), "reg%d", index);

   store->child = node;
   gpir_node_add_dep(&store->node, node, GPIR_DEP_INPUT);

   list_for_each_entry(gpir_reg, reg, &block->comp->reg_list, list) {
      if (reg->index == index) {
         store->reg = reg;
         list_addtail(&store->reg_link, &reg->defs_list);
         break;
      }
   }

   list_addtail(&node->list, &block->node_list);
   list_addtail(&store->node.list, &block->node_list);
   return node;
}

static void *gpir_node_create_dest(gpir_block *block, gpir_op op, nir_dest *dest)
{
   if (dest->is_ssa)
      return gpir_node_create_ssa(block, op, &dest->ssa);
   else
      return gpir_node_create_reg(block, op, &dest->reg);
}

static gpir_node *gpir_node_find(gpir_block *block, gpir_node *succ, nir_src *src)
{
   gpir_node *pred;

   if (src->is_ssa) {
      pred = block->comp->var_nodes[src->ssa->index];
      assert(pred);
   }
   else {
      pred = gpir_node_create(block, gpir_op_load_reg);
      list_addtail(&pred->list, &succ->list);

      gpir_load_node *load = gpir_node_to_load(pred);
      list_for_each_entry(gpir_reg, reg, &block->comp->reg_list, list) {
         if (reg->index == src->reg.reg->index) {
            load->reg = reg;
            list_addtail(&load->reg_link, &reg->uses_list);
            break;
         }
      }
   }

   return pred;
}

static int nir_to_gpir_opcodes[nir_num_opcodes] = {
   /* not supported */
   [0 ... nir_last_opcode] = -1,

   [nir_op_fmul] = gpir_op_mul,
   [nir_op_fadd] = gpir_op_add,
   [nir_op_fneg] = gpir_op_neg,
   [nir_op_fnot] = gpir_op_not,
   [nir_op_fmin] = gpir_op_min,
   [nir_op_fmax] = gpir_op_max,
   [nir_op_frcp] = gpir_op_rcp,
   [nir_op_frsq] = gpir_op_rsqrt,
   [nir_op_slt] = gpir_op_lt,
   [nir_op_sge] = gpir_op_ge,
   [nir_op_fcsel] = gpir_op_select,
   [nir_op_ffloor] = gpir_op_floor,
   [nir_op_fsign] = gpir_op_sign,
   [nir_op_seq] = gpir_op_eq,
   [nir_op_sne] = gpir_op_ne,
   [nir_op_fand] = gpir_op_min,
   [nir_op_for] = gpir_op_max,
   [nir_op_fabs] = gpir_op_abs,
};

static bool gpir_emit_alu(gpir_block *block, nir_instr *ni)
{
   nir_alu_instr *instr = nir_instr_as_alu(ni);
   int op = nir_to_gpir_opcodes[instr->op];

   if (op < 0) {
      gpir_error("unsupported nir_op: %s\n", nir_op_infos[instr->op].name);
      return false;
   }

   gpir_alu_node *node = gpir_node_create_dest(block, op, &instr->dest.dest);
   if (unlikely(!node))
      return false;

   unsigned num_child = nir_op_infos[instr->op].num_inputs;
   assert(num_child <= ARRAY_SIZE(node->children));
   node->num_child = num_child;

   for (int i = 0; i < num_child; i++) {
      nir_alu_src *src = instr->src + i;
      node->children_negate[i] = src->negate;

      gpir_node *child = gpir_node_find(block, &node->node, &src->src);
      node->children[i] = child;

      gpir_node_add_dep(&node->node, child, GPIR_DEP_INPUT);
   }

   return true;
}

static bool gpir_emit_intrinsic(gpir_block *block, nir_instr *ni)
{
   nir_intrinsic_instr *instr = nir_instr_as_intrinsic(ni);

   switch (instr->intrinsic) {
   case nir_intrinsic_load_input:
   {
      gpir_load_node *load =
         gpir_node_create_dest(block, gpir_op_load_attribute, &instr->dest);
      if (unlikely(!load))
         return false;

      load->index = nir_intrinsic_base(instr);
      load->component = nir_intrinsic_component(instr);

      return true;
   }
   case nir_intrinsic_load_uniform:
   {
      gpir_load_node *load =
         gpir_node_create_dest(block, gpir_op_load_uniform, &instr->dest);
      if (unlikely(!load))
         return false;

      int offset = nir_intrinsic_base(instr);
      offset += (int)nir_src_as_float(instr->src[0]);

      load->index = offset / 4;
      load->component = offset % 4;

      return true;
   }
   case nir_intrinsic_store_output:
   {
      gpir_store_node *store = gpir_node_create(block, gpir_op_store_varying);
      if (unlikely(!store))
         return false;
      list_addtail(&store->node.list, &block->node_list);

      store->index = nir_intrinsic_base(instr);
      store->component = nir_intrinsic_component(instr);

      gpir_node *child = gpir_node_find(block, &store->node, instr->src);
      store->child = child;
      gpir_node_add_dep(&store->node, child, GPIR_DEP_INPUT);

      return true;
   }
   default:
      gpir_error("unsupported nir_intrinsic_instr %d\n", instr->intrinsic);
      return false;
   }
}

static bool gpir_emit_load_const(gpir_block *block, nir_instr *ni)
{
   nir_load_const_instr *instr = nir_instr_as_load_const(ni);
   gpir_const_node *node =
      gpir_node_create_ssa(block, gpir_op_const, &instr->def);
   if (unlikely(!node))
      return false;

   assert(instr->def.bit_size == 32);
   assert(instr->def.num_components == 1);

   node->value.i = instr->value[0].i32;

   return true;
}

static bool gpir_emit_ssa_undef(gpir_block *block, nir_instr *ni)
{
   gpir_error("nir_ssa_undef_instr not support\n");
   return false;
}

static bool gpir_emit_tex(gpir_block *block, nir_instr *ni)
{
   gpir_error("nir_jump_instr not support\n");
   return false;
}

static bool gpir_emit_jump(gpir_block *block, nir_instr *ni)
{
   gpir_error("nir_jump_instr not support\n");
   return false;
}

static bool (*gpir_emit_instr[nir_instr_type_phi])(gpir_block *, nir_instr *) = {
   [nir_instr_type_alu]        = gpir_emit_alu,
   [nir_instr_type_intrinsic]  = gpir_emit_intrinsic,
   [nir_instr_type_load_const] = gpir_emit_load_const,
   [nir_instr_type_ssa_undef]  = gpir_emit_ssa_undef,
   [nir_instr_type_tex]        = gpir_emit_tex,
   [nir_instr_type_jump]       = gpir_emit_jump,
};

static gpir_block *gpir_block_create(gpir_compiler *comp)
{
   gpir_block *block = ralloc(comp, gpir_block);
   if (!block)
      return NULL;

   list_inithead(&block->node_list);
   list_inithead(&block->instr_list);

   return block;
}

static bool gpir_emit_block(gpir_compiler *comp, nir_block *nblock)
{
   gpir_block *block = gpir_block_create(comp);
   if (!block)
      return false;

   list_addtail(&block->list, &comp->block_list);
   block->comp = comp;

   nir_foreach_instr(instr, nblock) {
      assert(instr->type < nir_instr_type_phi);
      if (!gpir_emit_instr[instr->type](block, instr))
         return false;
   }

   return true;
}

static bool gpir_emit_if(gpir_compiler *comp, nir_if *nif)
{
   gpir_error("if nir_cf_node not support\n");
   return false;
}

static bool gpir_emit_loop(gpir_compiler *comp, nir_loop *nloop)
{
   gpir_error("loop nir_cf_node not support\n");
   return false;
}

static bool gpir_emit_function(gpir_compiler *comp, nir_function_impl *nfunc)
{
   gpir_error("function nir_cf_node not support\n");
   return false;
}

static bool gpir_emit_cf_list(gpir_compiler *comp, struct exec_list *list)
{
   foreach_list_typed(nir_cf_node, node, node, list) {
      bool ret;

      switch (node->type) {
      case nir_cf_node_block:
         ret = gpir_emit_block(comp, nir_cf_node_as_block(node));
         break;
      case nir_cf_node_if:
         ret = gpir_emit_if(comp, nir_cf_node_as_if(node));
         break;
      case nir_cf_node_loop:
         ret = gpir_emit_loop(comp, nir_cf_node_as_loop(node));
         break;
      case nir_cf_node_function:
         ret = gpir_emit_function(comp, nir_cf_node_as_function(node));
         break;
      default:
         gpir_error("unknown NIR node type %d\n", node->type);
         return false;
      }

      if (!ret)
         return false;
   }

   return true;
}

gpir_reg *gpir_create_reg(gpir_compiler *comp)
{
   gpir_reg *reg = ralloc(comp, gpir_reg);
   reg->index = comp->cur_reg++;
   list_addtail(&reg->list, &comp->reg_list);
   list_inithead(&reg->defs_list);
   list_inithead(&reg->uses_list);
   return reg;
}

static gpir_compiler *gpir_compiler_create(void *prog, unsigned num_reg, unsigned num_ssa)
{
   gpir_compiler *comp = rzalloc(prog, gpir_compiler);

   list_inithead(&comp->block_list);
   list_inithead(&comp->reg_list);

   for (int i = 0; i < num_reg; i++)
      gpir_create_reg(comp);

   comp->var_nodes = rzalloc_array(comp, gpir_node *, num_ssa);
   comp->prog = prog;
   return comp;
}

static int gpir_glsl_type_size(enum glsl_base_type type)
{
   /* only support GLSL_TYPE_FLOAT */
   assert(type == GLSL_TYPE_FLOAT);
   return 4;
}

bool gpir_compile_nir(struct lima_vs_shader_state *prog, struct nir_shader *nir)
{
   nir_function_impl *func = nir_shader_get_entrypoint(nir);
   gpir_compiler *comp = gpir_compiler_create(prog, func->reg_alloc, func->ssa_alloc);
   if (!comp)
      return false;

   comp->constant_base = nir->num_uniforms;
   prog->uniform_pending_offset = nir->num_uniforms * 16;

   if (!gpir_emit_cf_list(comp, &func->body))
      goto err_out0;

   gpir_node_print_prog_seq(comp);
   gpir_node_print_prog_dep(comp);

   if (!gpir_pre_rsched_lower_prog(comp))
      goto err_out0;

   if (!gpir_reduce_reg_pressure_schedule_prog(comp))
      goto err_out0;

   if (!gpir_post_rsched_lower_prog(comp))
      goto err_out0;

   if (!gpir_value_regalloc_prog(comp))
      goto err_out0;

   if (!gpir_physical_regalloc_prog(comp))
      goto err_out0;

   if (!gpir_schedule_prog(comp))
      goto err_out0;

   if (!gpir_codegen_prog(comp))
      goto err_out0;

   nir_foreach_variable(var, &nir->outputs) {
      if (var->data.location == VARYING_SLOT_POS)
         assert(var->data.driver_location == 0);

      struct lima_varying_info *v = prog->varying + var->data.driver_location;
      if (!v->components) {
         v->component_size = gpir_glsl_type_size(glsl_get_base_type(var->type));
         prog->num_varying++;
      }

      v->components += glsl_get_components(var->type);
   }

   ralloc_free(comp);
   return true;

err_out0:
   ralloc_free(comp);
   return false;
}

