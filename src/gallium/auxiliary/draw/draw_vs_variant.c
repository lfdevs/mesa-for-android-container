/**************************************************************************
 *
 * Copyright 2007 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

 /*
  * Authors:
  *   Keith Whitwell <keithw@vmware.com>
  */

#include "util/u_memory.h"
#include "util/u_math.h"
#include "draw/draw_context.h"
#include "draw/draw_private.h"
#include "draw/draw_vbuf.h"
#include "draw/draw_vertex.h"
#include "draw/draw_vs.h"
#include "translate/translate.h"


/* A first pass at incorporating vertex fetch/emit functionality into
 */
struct draw_vs_variant_generic {
   struct draw_vs_variant base;

   struct translate_key fetch_key;
   struct translate_key emit_key;

   unsigned temp_vertex_stride;
};



static void
vsvg_set_buffer(struct draw_context *draw,
                struct draw_vs_variant *variant,
                unsigned buffer,
                const void *ptr,
                unsigned stride,
                unsigned max_index)
{
   struct draw_vs_variant_generic *vsvg = (struct draw_vs_variant_generic *)variant;
   struct translate *fetch = draw_vs_get_fetch(draw, &vsvg->fetch_key);

   fetch->set_buffer(fetch, buffer, ptr, stride, max_index);
}


static const struct pipe_viewport_state *
find_viewport(struct draw_context *draw,
              char *buffer,
              unsigned vertex_idx,
              unsigned stride)
{
   int viewport_index_output =
      draw_current_shader_viewport_index_output(draw);
   const char *ptr = buffer + vertex_idx * stride;
   const unsigned *data = (const unsigned *) ptr;
   int viewport_index =
      draw_current_shader_uses_viewport_index(draw) ?
      data[viewport_index_output * 4] : 0;

   viewport_index = draw_clamp_viewport_idx(viewport_index);

   return &draw->viewports[viewport_index];
}


/* Mainly for debug at this stage:
 */
static void
do_rhw_viewport(struct draw_context *draw,
                struct draw_vs_variant_generic *vsvg,
                unsigned count,
                void *output_buffer)
{
   char *ptr = (char *)output_buffer;
   unsigned stride = vsvg->temp_vertex_stride;

   ptr += vsvg->base.vs->position_output * 4 * sizeof(float);

   for (unsigned j = 0; j < count; j++, ptr += stride) {
      const struct pipe_viewport_state *viewport =
         find_viewport(draw, (char*)output_buffer,
                       j, stride);
      const float *scale = viewport->scale;
      const float *trans = viewport->translate;
      float *data = (float *)ptr;
      float w = 1.0f / data[3];

      data[0] = data[0] * w * scale[0] + trans[0];
      data[1] = data[1] * w * scale[1] + trans[1];
      data[2] = data[2] * w * scale[2] + trans[2];
      data[3] = w;
   }
}


static void
do_viewport(struct draw_context *draw,
            struct draw_vs_variant_generic *vsvg,
            unsigned count,
            void *output_buffer)
{
   char *ptr = (char *)output_buffer;
   unsigned stride = vsvg->temp_vertex_stride;

   ptr += vsvg->base.vs->position_output * 4 * sizeof(float);

   for (unsigned j = 0; j < count; j++, ptr += stride) {
      const struct pipe_viewport_state *viewport =
         find_viewport(draw, (char*)output_buffer,
                       j, stride);
      const float *scale = viewport->scale;
      const float *trans = viewport->translate;
      float *data = (float *)ptr;

      data[0] = data[0] * scale[0] + trans[0];
      data[1] = data[1] * scale[1] + trans[1];
      data[2] = data[2] * scale[2] + trans[2];
   }
}


static void UTIL_CDECL
vsvg_run_elts(struct draw_context *draw,
              struct draw_vs_variant *variant,
              const unsigned *elts,
              unsigned count,
              void *output_buffer)
{
   struct draw_vs_variant_generic *vsvg = (struct draw_vs_variant_generic *)variant;
   unsigned temp_vertex_stride = vsvg->temp_vertex_stride;
   void *temp_buffer = MALLOC(align(count,4) * temp_vertex_stride +
                              DRAW_EXTRA_VERTICES_PADDING);
   struct translate *fetch = draw_vs_get_fetch(draw, &vsvg->fetch_key);
   struct translate *emit = draw_vs_get_emit(draw, &vsvg->emit_key);

   if (0) debug_printf("%s %d \n", __func__,  count);

   /* Want to do this in small batches for cache locality?
    */

   fetch->run_elts(fetch,
                   elts,
                   count,
                   draw->start_instance,
                   draw->instance_id,
                   temp_buffer);

   vsvg->base.vs->run_linear(draw,
                             vsvg->base.vs,
                             temp_buffer,
                             temp_buffer,
                             draw->pt.user.constants[MESA_SHADER_VERTEX],
                             count,
                             temp_vertex_stride,
                             temp_vertex_stride, NULL);

   /* FIXME: geometry shading? */

   if (vsvg->base.key.clip) {
      /* not really handling clipping, just do the rhw so we can
       * see the results...
       */
      do_rhw_viewport(draw, vsvg, count, temp_buffer);
   } else if (vsvg->base.key.viewport) {
      do_viewport(draw, vsvg, count, temp_buffer);
   }

   emit->set_buffer(emit, 0, temp_buffer, temp_vertex_stride, ~0);
   emit->set_buffer(emit, 1, &draw->rasterizer->point_size, 0, ~0);

   emit->run(emit,
             0, count,
             draw->start_instance,
             draw->instance_id,
             output_buffer);

   FREE(temp_buffer);
}


static void UTIL_CDECL
vsvg_run_linear(struct draw_context *draw,
                struct draw_vs_variant *variant,
                unsigned start,
                unsigned count,
                void *output_buffer)
{
   struct draw_vs_variant_generic *vsvg = (struct draw_vs_variant_generic *)variant;
   unsigned temp_vertex_stride = vsvg->temp_vertex_stride;
   void *temp_buffer = MALLOC(align(count,4) * temp_vertex_stride +
                              DRAW_EXTRA_VERTICES_PADDING);
   struct translate *fetch = draw_vs_get_fetch(draw, &vsvg->fetch_key);
   struct translate *emit = draw_vs_get_emit(draw, &vsvg->emit_key);

   if (0) debug_printf("%s %d %d (sz %d, %d)\n", __func__, start, count,
                       vsvg->base.key.output_stride,
                       temp_vertex_stride);

   fetch->run(fetch,
              start,
              count,
              draw->start_instance,
              draw->instance_id,
              temp_buffer);

   vsvg->base.vs->run_linear(draw,
                             vsvg->base.vs,
                             temp_buffer,
                             temp_buffer,
                             draw->pt.user.constants[MESA_SHADER_VERTEX],
                             count,
                             temp_vertex_stride,
                             temp_vertex_stride, NULL);

   if (vsvg->base.key.clip) {
      /* not really handling clipping, just do the rhw so we can
       * see the results...
       */
      do_rhw_viewport(draw, vsvg, count, temp_buffer);
   } else if (vsvg->base.key.viewport) {
      do_viewport(draw, vsvg, count, temp_buffer);
   }

   emit->set_buffer(emit, 0, temp_buffer, temp_vertex_stride, ~0);
   emit->set_buffer(emit, 1, &draw->rasterizer->point_size, 0, ~0);

   emit->run(emit,
             0, count,
             draw->start_instance,
             draw->instance_id,
             output_buffer);

   FREE(temp_buffer);
}


static void
vsvg_destroy(struct draw_vs_variant *variant)
{
   FREE(variant);
}


struct draw_vs_variant *
draw_vs_create_variant_generic(struct draw_context *draw,
                               struct draw_vertex_shader *vs,
                               const struct draw_vs_variant_key *key)
{
   struct draw_vs_variant_generic *vsvg = CALLOC_STRUCT(draw_vs_variant_generic);
   if (!vsvg)
      return NULL;

   vsvg->base.key = *key;
   vsvg->base.vs = vs;
   vsvg->base.set_buffer    = vsvg_set_buffer;
   vsvg->base.run_elts      = vsvg_run_elts;
   vsvg->base.run_linear    = vsvg_run_linear;
   vsvg->base.destroy       = vsvg_destroy;

   vsvg->temp_vertex_stride = MAX2(key->nr_inputs,
                                   draw_total_vs_outputs(draw)) * 4 * sizeof(float);

   vsvg->fetch_key.nr_elements = key->nr_inputs;
   vsvg->fetch_key.output_stride = vsvg->temp_vertex_stride;
   for (unsigned i = 0; i < key->nr_inputs; i++) {
      vsvg->fetch_key.element[i].type = TRANSLATE_ELEMENT_NORMAL;
      vsvg->fetch_key.element[i].input_format = key->element[i].in.format;
      vsvg->fetch_key.element[i].input_buffer = key->element[i].in.buffer;
      vsvg->fetch_key.element[i].input_offset = key->element[i].in.offset;
      vsvg->fetch_key.element[i].instance_divisor = 0;
      vsvg->fetch_key.element[i].output_format = PIPE_FORMAT_R32G32B32A32_FLOAT;
      vsvg->fetch_key.element[i].output_offset = i * 4 * sizeof(float);
      assert(vsvg->fetch_key.element[i].output_offset <
             vsvg->fetch_key.output_stride);
   }

   vsvg->emit_key.nr_elements = key->nr_outputs;
   vsvg->emit_key.output_stride = key->output_stride;
   for (unsigned i = 0; i < key->nr_outputs; i++) {
      if (key->element[i].out.format != EMIT_1F_PSIZE) {
         vsvg->emit_key.element[i].type = TRANSLATE_ELEMENT_NORMAL;
         vsvg->emit_key.element[i].input_format = PIPE_FORMAT_R32G32B32A32_FLOAT;
         vsvg->emit_key.element[i].input_buffer = 0;
         vsvg->emit_key.element[i].input_offset = key->element[i].out.vs_output * 4 * sizeof(float);
         vsvg->emit_key.element[i].instance_divisor = 0;
         vsvg->emit_key.element[i].output_format = draw_translate_vinfo_format(key->element[i].out.format);
         vsvg->emit_key.element[i].output_offset = key->element[i].out.offset;
         assert(vsvg->emit_key.element[i].input_offset <=
                vsvg->fetch_key.output_stride);
      } else {
         vsvg->emit_key.element[i].type = TRANSLATE_ELEMENT_NORMAL;
         vsvg->emit_key.element[i].input_format = PIPE_FORMAT_R32_FLOAT;
         vsvg->emit_key.element[i].input_buffer = 1;
         vsvg->emit_key.element[i].input_offset = 0;
         vsvg->emit_key.element[i].instance_divisor = 0;
         vsvg->emit_key.element[i].output_format = PIPE_FORMAT_R32_FLOAT;
         vsvg->emit_key.element[i].output_offset = key->element[i].out.offset;
      }
   }

   return &vsvg->base;
}
