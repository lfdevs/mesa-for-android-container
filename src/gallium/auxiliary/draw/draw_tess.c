/**************************************************************************
 *
 * Copyright 2020 Red Hat.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **************************************************************************/
#include "draw_tess.h"
#if DRAW_LLVM_AVAILABLE
#include "draw_llvm.h"
#endif

#include "tessellator/p_tessellator.h"
#include "nir/nir_to_tgsi_info.h"
#include "util/u_prim.h"
#include "util/u_math.h"
#include "util/u_memory.h"
#include "util/ralloc.h"
#if DRAW_LLVM_AVAILABLE
static inline int
draw_tes_get_input_index(int semantic, int index,
                         const struct tgsi_shader_info *input_info)
{
   int i;
   const uint8_t *input_semantic_names = input_info->output_semantic_name;
   const uint8_t *input_semantic_indices = input_info->output_semantic_index;
   for (i = 0; i < PIPE_MAX_SHADER_OUTPUTS; i++) {
      if (input_semantic_names[i] == semantic &&
          input_semantic_indices[i] == index)
         return i;
   }
   return -1;
}

#define DEBUG_INPUTS 0
static void
llvm_fetch_tcs_input(struct draw_context *draw,
                     const struct draw_tess_ctrl_shader *shader,
                     const struct draw_prim_info *input_prim_info,
                     unsigned prim_id,
                     unsigned num_vertices,
                     const float (*input_ptr)[4],
                     unsigned input_vertex_stride,
                     const struct tgsi_shader_info *input_info)
{
   float (*input_data)[32][NUM_TCS_INPUTS][TGSI_NUM_CHANNELS] = &draw->tcs.tcs_input->data;
   unsigned slot, i;
   int vs_slot;

   for (i = 0; i < num_vertices; i++) {
      const float (*input)[4];
      int vertex_idx = prim_id * num_vertices + i;
      if (input_prim_info->linear == false)
         vertex_idx = input_prim_info->elts[vertex_idx];
#if DEBUG_INPUTS
      debug_printf("%d) tcs vertex index = %d (prim idx = %d)\n",
                   i, prim_id, 0);
#endif
      input = (const float (*)[4])((const char *)input_ptr + (vertex_idx * input_vertex_stride));
      for (slot = 0, vs_slot = 0; slot < shader->info.num_inputs; ++slot) {
         vs_slot = draw_tes_get_input_index(
                                            shader->info.input_semantic_name[slot],
                                            shader->info.input_semantic_index[slot],
                                            input_info);
         if (vs_slot < 0) {
            debug_printf("VS/TCS signature mismatch!\n");
            (*input_data)[i][slot][0] = 0;
            (*input_data)[i][slot][1] = 0;
            (*input_data)[i][slot][2] = 0;
            (*input_data)[i][slot][3] = 0;
         } else {
            (*input_data)[i][slot][0] = input[vs_slot][0];
            (*input_data)[i][slot][1] = input[vs_slot][1];
            (*input_data)[i][slot][2] = input[vs_slot][2];
            (*input_data)[i][slot][3] = input[vs_slot][3];
#if DEBUG_INPUTS
            debug_printf("\t\t%p = %f %f %f %f\n", &(*input_data)[i][slot][0],
                         (*input_data)[i][slot][0],
                         (*input_data)[i][slot][1],
                         (*input_data)[i][slot][2],
                         (*input_data)[i][slot][3]);
#endif
            ++vs_slot;
         }
      }
   }
}

#define DEBUG_OUTPUTS 0
static void
llvm_store_tcs_output(struct draw_context *draw,
                      const struct draw_tess_ctrl_shader *shader,
                      unsigned prim_id,
                      struct draw_vertex_info *output_verts,
                      unsigned vert_start)
{
   float (*output_ptr)[4];
   float (*output_data)[32][PIPE_MAX_SHADER_INPUTS][TGSI_NUM_CHANNELS] = &draw->tcs.tcs_output->data;
   unsigned slot, i;
   unsigned num_vertices = shader->vertices_out;

   char *output = (char *)output_verts->verts->data;
   output += vert_start * output_verts->stride;

   for (i = 0; i < num_vertices; i++) {

#if DEBUG_OUTPUTS
      debug_printf("%d) tcs store vertex index = %d (prim idx = %d)\n",
                   i, prim_id, 0);
#endif
      output_ptr = (float(*)[4])(output + (i * output_verts->stride));

      for (slot = 0; slot < shader->info.num_outputs; ++slot) {
         output_ptr[slot][0] = (*output_data)[i][slot][0];
         output_ptr[slot][1] = (*output_data)[i][slot][1];
         output_ptr[slot][2] = (*output_data)[i][slot][2];
         output_ptr[slot][3] = (*output_data)[i][slot][3];
#if DEBUG_OUTPUTS
         debug_printf("\t\t%p = %f %f %f %f\n",
                      &output_ptr[slot][0],
                      output_ptr[slot][0],
                      output_ptr[slot][1],
                      output_ptr[slot][2],
                      output_ptr[slot][3]);
#endif
      }
   }
}

static void
llvm_tcs_run(struct draw_context *draw, uint32_t prim_id)
{
   draw->tcs.current_variant->jit_func(&draw->llvm->jit_resources[MESA_SHADER_TESS_CTRL],
                                       draw->tcs.tcs_input->data, draw->tcs.tcs_output->data, prim_id,
                                       draw->pt.vertices_per_patch, draw->pt.user.viewid);
}
#endif

/**
 * Execute tess ctrl shader.
 */
int draw_tess_ctrl_shader_run(struct draw_context *draw,
                              const struct draw_tess_ctrl_shader *shader,
                              const struct draw_vertex_info *input_verts,
                              const struct draw_prim_info *input_prims,
                              const struct tgsi_shader_info *input_info,
                              struct draw_vertex_info *output_verts,
                              struct draw_prim_info *output_prims)
{
   unsigned num_outputs = draw_total_tcs_outputs(draw);
   unsigned vertex_size = sizeof(struct vertex_header) + num_outputs * 4 * sizeof(float);
   unsigned num_patches = input_prims->count / draw->pt.vertices_per_patch;

   output_verts->vertex_size = vertex_size;
   output_verts->stride = output_verts->vertex_size;
   output_verts->verts = NULL;
   output_verts->count = 0;

   output_prims->linear = true;
   output_prims->start = input_prims->start;
   output_prims->elts = NULL;
   output_prims->count = 0;
   output_prims->prim = MESA_PRIM_PATCHES;
   output_prims->flags = 0;
   output_prims->primitive_lengths = NULL;
   output_prims->primitive_count = 0;

   if (draw->collect_statistics) {
      draw->statistics.hs_invocations += num_patches;
   }
#if DRAW_LLVM_AVAILABLE
   const float (*input)[4] = (const float (*)[4])input_verts->verts->data;
   unsigned input_stride = input_verts->vertex_size;

   unsigned first_patch = input_prims->start / draw->pt.vertices_per_patch;
   for (unsigned i = 0; i < num_patches; i++) {
      uint32_t vert_start = output_verts->count;

      output_verts->count += shader->vertices_out;

      llvm_fetch_tcs_input(draw, shader, input_prims, i, draw->pt.vertices_per_patch,
                           input, input_stride, input_info);

      llvm_tcs_run(draw, first_patch + i);

      uint32_t old_verts = util_align_npot(vert_start, 16);
      uint32_t new_verts = util_align_npot(output_verts->count, 16);
      uint32_t old_size = output_verts->vertex_size * old_verts;
      uint32_t new_size = output_verts->vertex_size * new_verts;
      output_verts->verts = REALLOC(output_verts->verts, old_size, new_size);

      llvm_store_tcs_output(draw, shader, i, output_verts, vert_start);
   }
#endif

   output_prims->primitive_count = num_patches;
   return 0;
}

#if DRAW_LLVM_AVAILABLE
#define DEBUG_INPUTS 0
static void
llvm_fetch_tes_input(struct draw_context *draw,
                     const struct draw_tess_eval_shader *shader,
                     const struct draw_prim_info *input_prim_info,
                     unsigned prim_id,
                     unsigned num_vertices,
                     const float (*input_ptr)[4],
                     unsigned input_vertex_stride,
                     const struct tgsi_shader_info *input_info)
{
   float (*input_data)[32][PIPE_MAX_SHADER_INPUTS][TGSI_NUM_CHANNELS] = &draw->tes.tes_input->data;
   unsigned slot, i;
   int vs_slot;

   for (i = 0; i < num_vertices; i++) {
      const float (*input)[4];
      int vertex_idx = prim_id * num_vertices + i;

      if (input_prim_info->linear == false)
         vertex_idx = input_prim_info->elts[vertex_idx];
#if DEBUG_INPUTS
      debug_printf("%d) tes vertex index = %d (prim idx = %d)\n",
                   i, prim_id, 0);
#endif
      input = (const float (*)[4])((const char *)input_ptr + (vertex_idx * input_vertex_stride));
      for (slot = 0, vs_slot = 0; slot < shader->info.num_inputs; ++slot) {
         vs_slot = draw_tes_get_input_index(
                                            shader->info.input_semantic_name[slot],
                                            shader->info.input_semantic_index[slot],
                                            input_info);
         if (vs_slot < 0) {
            debug_printf("TCS/TES signature mismatch!\n");
            (*input_data)[i][slot][0] = 0;
            (*input_data)[i][slot][1] = 0;
            (*input_data)[i][slot][2] = 0;
            (*input_data)[i][slot][3] = 0;
         } else {
            (*input_data)[i][slot][0] = input[vs_slot][0];
            (*input_data)[i][slot][1] = input[vs_slot][1];
            (*input_data)[i][slot][2] = input[vs_slot][2];
            (*input_data)[i][slot][3] = input[vs_slot][3];
#if DEBUG_INPUTS
            debug_printf("\t\t%p = %f %f %f %f\n",
                         &input[vs_slot][0],
                         (*input_data)[i][slot][0],
                         (*input_data)[i][slot][1],
                         (*input_data)[i][slot][2],
                         (*input_data)[i][slot][3]);
#endif
            ++vs_slot;
         }
      }
   }
}

static void
llvm_fetch_tess_factors(struct draw_context *draw,
                        unsigned patch_id,
                        unsigned num_vertices,
                        const float (*input_ptr)[4],
                        unsigned input_vertex_stride,
                        const struct tgsi_shader_info *input_info,
                        struct pipe_tessellation_factors *factors)
{
   int outer_slot = draw_tes_get_input_index(
      TGSI_SEMANTIC_TESSOUTER, 0, input_info);
   int inner_slot = draw_tes_get_input_index(
      TGSI_SEMANTIC_TESSINNER, 0, input_info);
   const float (*input)[4];
   input = (const float (*)[4])((const char *)input_ptr + ((patch_id * num_vertices) * input_vertex_stride));

   if (outer_slot != -1) {
      for (unsigned i = 0; i < 4; i++)
         factors->outer_tf[i] = input[outer_slot][i];
   } else {
      for (unsigned i = 0; i < 4; i++)
         factors->outer_tf[i] = draw->default_outer_tess_level[i];
   }
   if (inner_slot != -1) {
      for (unsigned i = 0; i < 2; i++)
         factors->inner_tf[i] = input[inner_slot][i];
   } else {
      for (unsigned i = 0; i < 2; i++)
         factors->inner_tf[i] = draw->default_inner_tess_level[i];
   }
}

static void
llvm_tes_run(struct draw_context *draw,
             uint32_t prim_id,
             uint32_t patch_vertices_in,
             struct pipe_tessellator_data *tess_data,
             struct pipe_tessellation_factors *tess_factors,
             struct vertex_header *output)
{
   draw->tes.current_variant->jit_func(&draw->llvm->jit_resources[MESA_SHADER_TESS_EVAL],
                                       draw->tes.tes_input->data, output, prim_id,
                                       tess_data->num_domain_points, tess_data->domain_points_u, tess_data->domain_points_v,
                                       tess_factors->outer_tf, tess_factors->inner_tf, patch_vertices_in,
                                       draw->pt.user.viewid);
}
#endif

/**
 * Execute tess eval shader.
 */
int draw_tess_eval_shader_run(struct draw_context *draw,
                              const struct draw_tess_eval_shader *shader,
                              unsigned num_input_vertices_per_patch,
                              const struct draw_vertex_info *input_verts,
                              const struct draw_prim_info *input_prims,
                              const struct tgsi_shader_info *input_info,
                              struct draw_vertex_info *output_verts,
                              struct draw_prim_info *output_prims,
                              uint32_t **patch_lengths,
                              uint16_t **elts_out)
{
   unsigned num_outputs = draw_total_tes_outputs(draw);
   unsigned vertex_size = sizeof(struct vertex_header) + num_outputs * 4 * sizeof(float);
   uint16_t *elts = NULL;
   output_verts->vertex_size = vertex_size;
   output_verts->stride = output_verts->vertex_size;
   output_verts->count = 0;
   output_verts->verts = NULL;

   output_prims->linear = false;
   output_prims->start = 0;
   output_prims->elts = NULL;
   output_prims->count = 0;
   output_prims->prim = get_tes_output_prim(shader);
   output_prims->flags = 0;
   output_prims->primitive_lengths = NULL;
   output_prims->primitive_count = 0;

   if (patch_lengths) {
      *patch_lengths = MALLOC(input_prims->primitive_count * sizeof(uint32_t));
   }

#if DRAW_LLVM_AVAILABLE
   const float (*input)[4] = (const float (*)[4])input_verts->verts->data;
   unsigned input_stride = input_verts->vertex_size;

   struct pipe_tessellation_factors factors;
   struct pipe_tessellator_data data = { 0 };
   struct pipe_tessellator *ptess = p_tess_init(shader->prim_mode,
                                                shader->spacing,
                                                !shader->vertex_order_cw,
                                                shader->point_mode);
   unsigned first_patch = input_prims->start / draw->pt.vertices_per_patch;
   for (unsigned i = 0; i < input_prims->primitive_count; i++) {
      uint32_t vert_start = output_verts->count;
      uint32_t prim_start = output_prims->primitive_count;
      uint32_t elt_start = output_prims->count;

      llvm_fetch_tess_factors(draw, i, num_input_vertices_per_patch,
                              input, input_stride, input_info, &factors);

      /**
       * Tessellate with the factors for this primitive.
       * Make sure subnormals are not flushed to zero during tessellation.
       * This is the behavior required by D3D11. OpenGL doesn't care.
       */
      unsigned fpstate = util_fpstate_get();
      util_fpstate_set(draw->fpstate);  /* do not flush subnormals */
      p_tessellate(ptess, &factors, &data);
      util_fpstate_set(fpstate);                /* flush subnormals again */

      if (data.num_domain_points == 0)
         continue;

      uint32_t old_verts = vert_start;
      uint32_t new_verts = vert_start + util_align_npot(data.num_domain_points, 4);
      uint32_t old_size = output_verts->vertex_size * old_verts;
      uint32_t new_size = output_verts->vertex_size * new_verts;
      output_verts->verts = REALLOC(output_verts->verts, old_size, new_size);

      output_verts->count += data.num_domain_points;

      output_prims->count += data.num_indices;
      elts = REALLOC(elts, elt_start * sizeof(uint16_t),
                     output_prims->count * sizeof(uint16_t));

      for (uint32_t i = 0; i < data.num_indices; i++)
         elts[elt_start + i] = vert_start + data.indices[i];

      llvm_fetch_tes_input(draw, shader, input_prims, i, num_input_vertices_per_patch,
                           input, input_stride, input_info);
      /* run once per primitive? */
      char *output = (char *)output_verts->verts;
      output += vert_start * vertex_size;
      llvm_tes_run(draw, first_patch + i, num_input_vertices_per_patch, &data, &factors, (struct vertex_header *)output);

      if (draw->collect_statistics) {
         draw->statistics.ds_invocations += data.num_domain_points;
      }

      uint32_t prim_len = u_prim_vertex_count(output_prims->prim)->min;
      uint32_t prims_per_patch = data.num_indices / prim_len;
      output_prims->primitive_count += prims_per_patch;
      if (patch_lengths) {
         (*patch_lengths)[i] = prims_per_patch;
      }

      output_prims->primitive_lengths = REALLOC(output_prims->primitive_lengths, prim_start * sizeof(uint32_t),
                                                output_prims->primitive_count * sizeof(uint32_t));
      for (uint32_t i = prim_start; i < output_prims->primitive_count; i++) {
         output_prims->primitive_lengths[i] = prim_len;
      }
   }
   p_tess_destroy(ptess);
#endif

   *elts_out = elts;
   output_prims->elts = elts;
   return 0;
}

struct draw_tess_ctrl_shader *
draw_create_tess_ctrl_shader(struct draw_context *draw,
                             const struct pipe_shader_state *state)
{
#if DRAW_LLVM_AVAILABLE
   bool use_llvm = draw->llvm != NULL;
   struct llvm_tess_ctrl_shader *llvm_tcs = NULL;
#endif
   struct draw_tess_ctrl_shader *tcs;

#if DRAW_LLVM_AVAILABLE
   if (use_llvm) {
      llvm_tcs = CALLOC_STRUCT(llvm_tess_ctrl_shader);

      if (!llvm_tcs)
         return NULL;

      tcs = &llvm_tcs->base;

      util_shader_variant_list_init(&llvm_tcs->variants);
   } else
#endif
   {
      tcs = CALLOC_STRUCT(draw_tess_ctrl_shader);
   }

   if (!tcs)
      return NULL;

   tcs->state = *state;

   nir_tgsi_scan_shader(state->ir.nir, &tcs->info, true);

   tcs->vector_length = 4;
   tcs->vertices_out = tcs->info.properties[TGSI_PROPERTY_TCS_VERTICES_OUT];
#if DRAW_LLVM_AVAILABLE
   if (use_llvm) {
      llvm_tcs->variant_key_size =
         draw_tcs_llvm_variant_key_size(
                                        tcs->info.file_max[TGSI_FILE_SAMPLER]+1,
                                        tcs->info.file_max[TGSI_FILE_SAMPLER_VIEW]+1,
                                        tcs->info.file_max[TGSI_FILE_IMAGE]+1);
   }
#endif
   return tcs;
}

void draw_bind_tess_ctrl_shader(struct draw_context *draw,
                                struct draw_tess_ctrl_shader *dtcs)
{
   draw_do_flush(draw, DRAW_FLUSH_STATE_CHANGE);
   if (dtcs) {
      draw->tcs.tess_ctrl_shader = dtcs;
   } else {
      draw->tcs.tess_ctrl_shader = NULL;
   }
}

void draw_delete_tess_ctrl_shader(struct draw_context *draw,
                                  struct draw_tess_ctrl_shader *dtcs)
{
   if (!dtcs)
      return;

#if DRAW_LLVM_AVAILABLE
   if (draw->llvm) {
      struct llvm_tess_ctrl_shader *shader = llvm_tess_ctrl_shader(dtcs);

      util_shader_variant_list_destroy(&draw->llvm->tcs_opts,
                                       &shader->variants);
   }
#endif

   if (dtcs->state.type == PIPE_SHADER_IR_NIR && dtcs->state.ir.nir)
      ralloc_free(dtcs->state.ir.nir);
   FREE(dtcs);
}

struct draw_tess_eval_shader *
draw_create_tess_eval_shader(struct draw_context *draw,
                             const struct pipe_shader_state *state)
{
#if DRAW_LLVM_AVAILABLE
   bool use_llvm = draw->llvm != NULL;
   struct llvm_tess_eval_shader *llvm_tes = NULL;
#endif
   struct draw_tess_eval_shader *tes;

#if DRAW_LLVM_AVAILABLE
   if (use_llvm) {
      llvm_tes = CALLOC_STRUCT(llvm_tess_eval_shader);

      if (!llvm_tes)
         return NULL;

      tes = &llvm_tes->base;
      util_shader_variant_list_init(&llvm_tes->variants);
   } else
#endif
   {
      tes = CALLOC_STRUCT(draw_tess_eval_shader);
   }

   if (!tes)
      return NULL;

   tes->state = *state;

   nir_tgsi_scan_shader(state->ir.nir, &tes->info, true);

   tes->prim_mode = tes->info.properties[TGSI_PROPERTY_TES_PRIM_MODE];
   tes->spacing = tes->info.properties[TGSI_PROPERTY_TES_SPACING];
   tes->vertex_order_cw = tes->info.properties[TGSI_PROPERTY_TES_VERTEX_ORDER_CW];
   tes->point_mode = tes->info.properties[TGSI_PROPERTY_TES_POINT_MODE];

   tes->vector_length = 4;

   tes->position_output = -1;
   bool found_clipvertex = false;
   for (unsigned i = 0; i < tes->info.num_outputs; i++) {
      if (tes->info.output_semantic_name[i] == TGSI_SEMANTIC_POSITION &&
          tes->info.output_semantic_index[i] == 0)
         tes->position_output = i;
      if (tes->info.output_semantic_name[i] == TGSI_SEMANTIC_VIEWPORT_INDEX)
         tes->viewport_index_output = i;
      if (tes->info.output_semantic_name[i] == TGSI_SEMANTIC_CLIPVERTEX &&
          tes->info.output_semantic_index[i] == 0) {
         found_clipvertex = true;
         tes->clipvertex_output = i;
      }
      if (tes->info.output_semantic_name[i] == TGSI_SEMANTIC_CLIPDIST) {
         assert(tes->info.output_semantic_index[i] <
                      PIPE_MAX_CLIP_OR_CULL_DISTANCE_ELEMENT_COUNT);
         tes->ccdistance_output[tes->info.output_semantic_index[i]] = i;
      }
   }
   if (!found_clipvertex)
      tes->clipvertex_output = tes->position_output;

#if DRAW_LLVM_AVAILABLE
   if (use_llvm) {
      llvm_tes->variant_key_size =
         draw_tes_llvm_variant_key_size(
                                        tes->info.file_max[TGSI_FILE_SAMPLER]+1,
                                        tes->info.file_max[TGSI_FILE_SAMPLER_VIEW]+1,
                                        tes->info.file_max[TGSI_FILE_IMAGE]+1);
   }
#endif
   return tes;
}

void draw_bind_tess_eval_shader(struct draw_context *draw,
                                struct draw_tess_eval_shader *dtes)
{
   draw_do_flush(draw, DRAW_FLUSH_STATE_CHANGE);
   if (dtes) {
      draw->tes.tess_eval_shader = dtes;
      draw->tes.num_tes_outputs = dtes->info.num_outputs;
      draw->tes.position_output = dtes->position_output;
      draw->tes.clipvertex_output = dtes->clipvertex_output;
   } else {
      draw->tes.tess_eval_shader = NULL;
   }
}

void draw_delete_tess_eval_shader(struct draw_context *draw,
                                  struct draw_tess_eval_shader *dtes)
{
   if (!dtes)
      return;

#if DRAW_LLVM_AVAILABLE
   if (draw->llvm) {
      struct llvm_tess_eval_shader *shader = llvm_tess_eval_shader(dtes);

      util_shader_variant_list_destroy(&draw->llvm->tes_opts,
                                       &shader->variants);
   }
#endif
   if (dtes->state.type == PIPE_SHADER_IR_NIR && dtes->state.ir.nir)
      ralloc_free(dtes->state.ir.nir);
   FREE(dtes);
}

enum mesa_prim get_tes_output_prim(const struct draw_tess_eval_shader *shader)
{
   if (shader->point_mode)
      return MESA_PRIM_POINTS;
   else if (shader->prim_mode == MESA_PRIM_LINES)
      return MESA_PRIM_LINES;
   else
      return MESA_PRIM_TRIANGLES;
}

bool draw_tess_init(struct draw_context *draw)
{
#if DRAW_LLVM_AVAILABLE
   draw->tcs.tcs_input = align_calloc(sizeof(struct draw_tcs_inputs), 16);
   draw->tcs.tcs_output = align_calloc(sizeof(struct draw_tcs_outputs), 16);
   draw->tes.tes_input = align_calloc(sizeof(struct draw_tes_inputs), 16);

   if (!draw->tcs.tcs_input || !draw->tcs.tcs_output || !draw->tes.tes_input)
      return false;
#endif
   return true;
}

void draw_tess_destroy(struct draw_context *draw)
{
#if DRAW_LLVM_AVAILABLE
   align_free(draw->tcs.tcs_input);
   align_free(draw->tcs.tcs_output);
   align_free(draw->tes.tes_input);
#endif
}
