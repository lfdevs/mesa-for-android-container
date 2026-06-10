/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco.c
 *
 * \brief Main compiler interface.
 */

#include "compiler/glsl_types.h"
#include "nir_serialize.h"
#include "pco.h"
#include "pco_internal.h"
#include "util/blob.h"
#include "util/hash_table.h"
#include "util/list.h"
#include "util/macros.h"
#include "util/ralloc.h"
#include "util/shader_stats.h"

#include <assert.h>
#include <stdbool.h>

/**
 * \brief PCO compiler context destructor.
 *
 * \param[in,out] ptr PCO compiler context pointer.
 */
static void pco_ctx_destructor(UNUSED void *ptr)
{
   glsl_type_singleton_decref();
}

/**
 * \brief Allocates and sets up a PCO compiler context.
 *
 * \param[in] dev_info Device info.
 * \param[in] dev_runtime_info Device runtime info.
 * \param[in] mem_ctx Ralloc memory allocation context.
 * \return The PCO compiler context, or NULL on failure.
 */
pco_ctx *pco_ctx_create(const struct pvr_device_info *dev_info,
                        const struct pvr_device_runtime_info *dev_runtime_info,
                        void *mem_ctx)
{
   pco_ctx *ctx = rzalloc_size(mem_ctx, sizeof(*ctx));

   ctx->dev_info = dev_info;
   ctx->dev_runtime_info = dev_runtime_info;

   pco_debug_init();

#ifndef NDEBUG
   /* Ensure NIR debug variables are processed. */
   nir_process_debug_variable();
#endif /* NDEBUG */

   glsl_type_singleton_init_or_ref();
   ralloc_set_destructor(ctx, pco_ctx_destructor);

   return ctx;
}

void pco_ctx_setup_usclib(pco_ctx *ctx, const void *data, unsigned size)
{
   struct blob_reader blob_reader;
   blob_reader_init(&blob_reader, data, size);
   ctx->usclib = nir_deserialize(ctx, pco_nir_options(), &blob_reader);
}

/**
 * \brief Updates the device info for a PCO compiler context.
 *
 * \param[in,out] ctx PCO compiler context.
 * \param[in] dev_info Device info.
 */
void pco_ctx_update_dev_info(pco_ctx *ctx,
                             const struct pvr_device_info *dev_info)
{
   ctx->dev_info = dev_info;
}

/**
 * \brief Allocates and sets up a PCO shader from a NIR shader.
 *
 * \param[in] ctx PCO compiler context.
 * \param[in] nir The NIR shader.
 * \return The PCO shader, or NULL on failure.
 */
pco_shader *pco_shader_create(pco_ctx *ctx, nir_shader *nir, void *mem_ctx)
{
   pco_shader *shader = rzalloc_size(mem_ctx, sizeof(*shader));

   shader->ctx = ctx;
   shader->nir = nir;
   shader->stage = nir->info.stage;
   shader->name = ralloc_strdup(shader, nir->info.name);
   shader->is_internal = nir->info.internal;
   shader->is_grouped = false;
   list_inithead(&shader->funcs);

   return shader;
}

/**
 * \brief Sets up a PCO cf node.
 *
 * \param[in,out] cf_node PCO cf node.
 * \param[in] type CF node type.
 */
static inline void init_cf_node(pco_cf_node *cf_node,
                                enum pco_cf_node_type type)
{
   cf_node->type = type;
   cf_node->parent = NULL;
}

/**
 * \brief Allocates and sets up a PCO function.
 *
 * \param[in,out] shader PCO shader.
 * \param[in] type The function type.
 * \param[in] num_params The number of parameters.
 * \return The PCO function, or NULL on failure.
 */
pco_func *pco_func_create(pco_shader *shader,
                          enum pco_func_type type,
                          unsigned num_params)
{
   pco_func *func = rzalloc_size(shader, sizeof(*func));
   pco_func *preamble = pco_preamble(shader);

   /* Add the function to the shader; preamble goes first, then entrypoint.
    * The rest of the functions will get appended.
    */
   if (type == PCO_FUNC_TYPE_PREAMBLE) {
      assert(!preamble);
      list_add(&func->link, &shader->funcs);
   } else if (type == PCO_FUNC_TYPE_ENTRYPOINT) {
      assert(!pco_entrypoint(shader));
      if (!preamble)
         list_add(&func->link, &shader->funcs);
      else
         list_add(&func->link, &preamble->link);
   } else {
      list_addtail(&func->link, &shader->funcs);
   }

   init_cf_node(&func->cf_node, PCO_CF_NODE_TYPE_FUNC);
   func->parent_shader = shader;
   func->type = type;
   func->index = shader->next_func++;

   list_inithead(&func->body);

   func->num_params = num_params;
   if (num_params) {
      func->params =
         rzalloc_array_size(func, sizeof(*func->params), num_params);
   }

   func->vec_infos = _mesa_hash_table_u64_create(func);

   func->enc_offset = ~0U;

   return func;
}

/**
 * \brief Allocates and sets up a PCO block.
 *
 * \param[in,out] func Parent function.
 * \return The PCO block, or NULL on failure.
 */
pco_block *pco_block_create(pco_func *func)
{
   pco_block *block = rzalloc_size(func, sizeof(*block));

   init_cf_node(&block->cf_node, PCO_CF_NODE_TYPE_BLOCK);
   block->parent_func = func;
   list_inithead(&block->instrs);
   block->index = func->next_block++;

   return block;
}

/**
 * \brief Allocates and sets up a PCO if construct.
 *
 * \param[in,out] func Parent function.
 * \return The PCO if construct, or NULL on failure.
 */
pco_if *pco_if_create(pco_func *func)
{
   pco_if *pif = rzalloc_size(func, sizeof(*pif));

   init_cf_node(&pif->cf_node, PCO_CF_NODE_TYPE_IF);
   pif->parent_func = func;
   list_inithead(&pif->prologue);
   list_inithead(&pif->then_body);
   list_inithead(&pif->interlogue);
   list_inithead(&pif->else_body);
   list_inithead(&pif->epilogue);
   pif->index = func->next_if++;

   return pif;
}

/**
 * \brief Allocates and sets up a PCO loop.
 *
 * \param[in,out] func Parent function.
 * \return The PCO loop, or NULL on failure.
 */
pco_loop *pco_loop_create(pco_func *func)
{
   pco_loop *loop = rzalloc_size(func, sizeof(*loop));

   init_cf_node(&loop->cf_node, PCO_CF_NODE_TYPE_LOOP);
   loop->parent_func = func;
   list_inithead(&loop->prologue);
   list_inithead(&loop->body);
   list_inithead(&loop->interlogue);
   list_inithead(&loop->epilogue);
   loop->index = func->next_loop++;

   return loop;
}

/**
 * \brief Allocates and sets up a PCO instruction.
 *
 * \param[in,out] func Parent function.
 * \param[in] num_dests Number of destinations.
 * \param[in] num_srcs Number of sources.
 * \return The PCO instruction, or NULL on failure.
 */
pco_instr *
pco_instr_create(pco_func *func, unsigned num_dests, unsigned num_srcs)
{
   pco_instr *instr;
   unsigned size = sizeof(*instr);
   size += num_dests * sizeof(*instr->dest);
   size += num_srcs * sizeof(*instr->src);

   instr = rzalloc_size(func, size);

   instr->parent_func = func;

   instr->num_dests = num_dests;
   instr->dest = (pco_ref *)(instr + 1);

   instr->num_srcs = num_srcs;
   instr->src = instr->dest + num_dests;

   list_inithead(&instr->phi_srcs);

   instr->index = func->next_instr++;

   return instr;
}

/**
 * \brief Allocates and sets up a PCO instruction group.
 *
 * \param[in,out] func Parent function.
 * \return The PCO instruction group, or NULL on failure.
 */
pco_igrp *pco_igrp_create(pco_func *func)
{
   pco_igrp *igrp = rzalloc_size(func, sizeof(*igrp));

   igrp->parent_func = func;
   igrp->index = func->next_igrp++;

   return igrp;
}

/**
 * \brief Deletes a PCO instruction.
 *
 * \param[in,out] instr PCO instruction.
 */
void pco_instr_delete(pco_instr *instr)
{
   list_del(&instr->link);
   ralloc_free(instr);
}

/**
 * \brief Returns the shader data.
 *
 * \param[in] shader PCO shader.
 */
pco_data *pco_shader_data(pco_shader *shader)
{
   return &shader->data;
}

/**
 * \brief Returns precompilation data for a shader.
 *
 * \param[in] shader PCO shader.
 * \return The precompilation data.
 */
pco_precomp_data pco_get_precomp_data(pco_shader *shader)
{
   assert(pco_shader_binary_size(shader));

   unsigned size_dwords = pco_shader_binary_size(shader) / sizeof(uint32_t);
   assert(size_dwords <= UINT16_MAX);

   return (pco_precomp_data){
      .temps = shader->data.common.temps,
      .vtxins = shader->data.common.vtxins,
      .coeffs = shader->data.common.coeffs,
      .shareds = shader->data.common.shareds,
      .size_dwords = size_dwords,
   };
}

/**
 * \brief Returns statistics for a shader.
 *
 * \param[in] shader PCO shader.
 * \return The shader statistics.
 */
struct pvr_stats pco_get_pvr_stats(pco_shader *shader)
{
   assert(shader->is_grouped);

   unsigned shader_size = pco_shader_binary_size(shader);
   assert(shader_size > 0);

   unsigned loop_count = 0;
   unsigned igrp_count = 0;

   unsigned main_count = 0;
   unsigned bitwise_count = 0;
   unsigned control_count = 0;

   pco_foreach_func_in_shader (func, shader) {
      pco_foreach_loop_in_func (loop, func) {
         loop_count++;
      }
      pco_foreach_igrp_in_func (igrp, func) {
         igrp_count++;
         switch (igrp->hdr.alutype) {
         case PCO_ALUTYPE_MAIN:
            main_count++;
            break;

         case PCO_ALUTYPE_BITWISE:
            bitwise_count++;
            break;

         case PCO_ALUTYPE_CONTROL:
            control_count++;
            break;

         default:
            UNREACHABLE("Invalid pco_alutype");
         }
      }
   }

   return (struct pvr_stats){
      .isa = PVR_STAT_ROGUE,
      .rogue = (struct rogue_stats) {
         .code_size = shader_size,
         .scratch_size = shader->data.common.scratch,
         .spill_count = shader->data.common.spilled_temps,
         .temp_count = shader->data.common.temps,
         .vtxin_count = shader->data.common.vtxins,
         .loop_count = loop_count,
         .inst_group_count = igrp_count,
         .main_inst_group_count = main_count,
         .bitwise_inst_group_count = bitwise_count,
         .control_inst_group_count = control_count,
      },
   };
}

enum pco_pck_format pco_pipe_to_pck_format(enum pipe_format format,
                                           bool *scale,
                                           bool *roundzero,
                                           bool *split)
{
   enum pco_pck_format pck_format = ~0;
   *scale = false;
   *roundzero = false;
   *split = false;

   switch (format) {
   case PIPE_FORMAT_R8_UNORM:
   case PIPE_FORMAT_R8G8_UNORM:
   case PIPE_FORMAT_R8G8B8_UNORM:
   case PIPE_FORMAT_R8G8B8A8_UNORM:
      pck_format = PCO_PCK_FORMAT_U8888;
      *scale = true;
      break;

   case PIPE_FORMAT_R8_SNORM:
   case PIPE_FORMAT_R8G8_SNORM:
   case PIPE_FORMAT_R8G8B8_SNORM:
   case PIPE_FORMAT_R8G8B8A8_SNORM:
      pck_format = PCO_PCK_FORMAT_S8888;
      *scale = true;
      break;

   case PIPE_FORMAT_R11G11B10_FLOAT:
      pck_format = PCO_PCK_FORMAT_F111110;
      break;

   /* TODO: better way to do the 1x2 component. */
   case PIPE_FORMAT_R10G10B10A2_UNORM:
      pck_format = PCO_PCK_FORMAT_U1010102;
      *scale = true;
      break;

   /* TODO: better way to do the 1x2 component. */
   case PIPE_FORMAT_R10G10B10A2_SNORM:
      pck_format = PCO_PCK_FORMAT_S1010102;
      *scale = true;
      break;

   case PIPE_FORMAT_R16_FLOAT:
   case PIPE_FORMAT_R16G16_FLOAT:
   case PIPE_FORMAT_R16G16B16_FLOAT:
   case PIPE_FORMAT_R16G16B16A16_FLOAT:
      pck_format = PCO_PCK_FORMAT_F16F16;
      *split = true;
      break;

   case PIPE_FORMAT_R16_UNORM:
   case PIPE_FORMAT_R16G16_UNORM:
   case PIPE_FORMAT_R16G16B16_UNORM:
   case PIPE_FORMAT_R16G16B16A16_UNORM:
      pck_format = PCO_PCK_FORMAT_U1616;
      *scale = true;
      *split = true;
      break;

   case PIPE_FORMAT_R16_SNORM:
   case PIPE_FORMAT_R16G16_SNORM:
   case PIPE_FORMAT_R16G16B16_SNORM:
   case PIPE_FORMAT_R16G16B16A16_SNORM:
      pck_format = PCO_PCK_FORMAT_S1616;
      *scale = true;
      *split = true;
      break;

   default:
      break;
   }

   return pck_format;
}
