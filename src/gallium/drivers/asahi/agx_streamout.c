/*
 * Copyright 2023 Alyssa Rosenzweig
 * Copyright 2022 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_xfb_info.h"
#include "util/u_draw.h"
#include "util/u_dump.h"
#include "util/u_prim.h"
#include "agx_state.h"

static struct pipe_stream_output_target *
agx_create_stream_output_target(struct pipe_context *pctx,
                                struct pipe_resource *prsc,
                                unsigned buffer_offset, unsigned buffer_size)
{
   struct pipe_stream_output_target *target;

   target = &rzalloc(pctx, struct agx_streamout_target)->base;

   if (!target)
      return NULL;

   pipe_reference_init(&target->reference, 1);
   pipe_resource_reference(&target->buffer, prsc);

   target->context = pctx;
   target->buffer_offset = buffer_offset;
   target->buffer_size = buffer_size;

   return target;
}

static void
agx_stream_output_target_destroy(struct pipe_context *pctx,
                                 struct pipe_stream_output_target *target)
{
   pipe_resource_reference(&target->buffer, NULL);
   ralloc_free(target);
}

static void
agx_set_stream_output_targets(struct pipe_context *pctx, unsigned num_targets,
                              struct pipe_stream_output_target **targets,
                              const unsigned *offsets)
{
   struct agx_context *ctx = agx_context(pctx);
   struct agx_streamout *so = &ctx->streamout;

   assert(num_targets <= ARRAY_SIZE(so->targets));

   for (unsigned i = 0; i < num_targets; i++) {
      /* From the Gallium documentation:
       *
       *    -1 means the buffer should be appended to, and everything else sets
       *    the internal offset.
       *
       * We append regardless, so just check for != -1. Yes, using a negative
       * sentinel value with an unsigned type is bananas. But it's in the
       * Gallium contract and it will work out fine. Probably should be
       * redefined to be ~0 instead of -1 but it doesn't really matter.
       */
      if (offsets[i] != -1)
         agx_so_target(targets[i])->offset = offsets[i];

      pipe_so_target_reference(&so->targets[i], targets[i]);
   }

   for (unsigned i = num_targets; i < so->num_targets; i++)
      pipe_so_target_reference(&so->targets[i], NULL);

   so->num_targets = num_targets;
}

static struct pipe_stream_output_target *
get_target(struct agx_context *ctx, unsigned buffer)
{
   if (buffer < ctx->streamout.num_targets)
      return ctx->streamout.targets[buffer];
   else
      return NULL;
}

/*
 * Return the address of the indexed streamout buffer. This will be
 * pushed into the streamout shader.
 */
uint64_t
agx_batch_get_so_address(struct agx_batch *batch, unsigned buffer,
                         uint32_t *size)
{
   struct pipe_stream_output_target *target = get_target(batch->ctx, buffer);

   /* If there's no target, don't write anything */
   if (!target) {
      *size = 0;
      return 0;
   }

   /* Otherwise, write the target */
   struct pipe_stream_output_info *so =
      &batch->ctx->stage[PIPE_SHADER_VERTEX].shader->base.stream_output;

   struct agx_resource *rsrc = agx_resource(target->buffer);
   agx_batch_writes(batch, rsrc);

   /* The amount of space left depends how much we've already consumed */
   unsigned stride = so->stride[buffer] * 4;
   uint32_t offset = agx_so_target(target)->offset * stride;

   *size = offset < target->buffer_size ? (target->buffer_size - offset) : 0;
   return rsrc->bo->ptr.gpu + target->buffer_offset + offset;
}

void
agx_draw_vbo_from_xfb(struct pipe_context *pctx,
                      const struct pipe_draw_info *info, unsigned drawid_offset,
                      const struct pipe_draw_indirect_info *indirect)
{
   struct pipe_draw_start_count_bias draw = {
      .start = 0,
      .count = agx_so_target(indirect->count_from_stream_output)->offset,
   };

   pctx->draw_vbo(pctx, info, drawid_offset, NULL, &draw, 1);
}

static uint32_t
xfb_prims_for_vertices(enum mesa_prim mode, unsigned verts)
{
   uint32_t prims = u_decomposed_prims_for_vertices(mode, verts);

   /* The GL spec isn't super clear about this, but it implies that quads are
    * supposed to be tessellated into primitives and piglit
    * (ext_transform_feedback-tessellation quads) checks this.
    */
   if (u_decomposed_prim(mode) == MESA_PRIM_QUADS)
      prims *= 2;

   return prims;
}

/*
 * Launch a streamout pipeline.
 */
void
agx_launch_so(struct pipe_context *pctx, const struct pipe_draw_info *info,
              const struct pipe_draw_start_count_bias *draw,
              uint64_t index_buffer)
{
   struct agx_context *ctx = agx_context(pctx);

   /* Break recursion from draw_vbo creating draw calls below: Do not do a
    * streamout draw for a streamout draw.
    */
   if (ctx->streamout.key.active)
      return;

   /* Configure the below draw to launch streamout rather than a regular draw */
   ctx->streamout.key.active = true;
   ctx->dirty |= AGX_DIRTY_XFB;

   ctx->streamout.key.index_size = info->index_size;
   ctx->streamout.key.mode = info->mode;
   ctx->streamout.key.flatshade_first = ctx->rast->base.flatshade_first;
   ctx->streamout.params.index_buffer = index_buffer;

   /* Ignore provoking vertex for modes that don't depend on the provoking
    * vertex, to reduce shader variants.
    */
   if (info->mode != MESA_PRIM_TRIANGLE_STRIP)
      ctx->streamout.key.flatshade_first = false;

   /* Determine how many vertices are XFB there will be */
   unsigned num_outputs =
      u_stream_outputs_for_vertices(info->mode, draw->count);
   unsigned count = draw->count;
   u_trim_pipe_prim(info->mode, &count);

   ctx->streamout.params.base_vertex =
      info->index_size ? draw->index_bias : draw->start;
   ctx->streamout.params.num_vertices = count;

   /* Streamout runs as a vertex shader with rasterizer discard */
   void *saved_rast = ctx->rast;
   pctx->bind_rasterizer_state(
      pctx, util_blitter_get_discard_rasterizer_state(ctx->blitter));

   /* Dispatch a grid of points, this is compute-like */
   util_draw_arrays_instanced(pctx, MESA_PRIM_POINTS, 0, num_outputs, 0,
                              info->instance_count);
   pctx->bind_rasterizer_state(pctx, saved_rast);

   /*
    * Finally, if needed, update the counter of primitives written. The spec
    * requires:
    *
    *    If recording the vertices of a primitive to the buffer objects being
    *    used for transform feedback purposes would result in [overflow]...
    *    the counter corresponding to the asynchronous query target
    *    TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN (see section 13.4) is not
    *    incremented.
    *
    * So clamp the number of primitives generated to the number of primitives
    * we actually have space to write.
    */
   if (ctx->tf_prims_generated) {
      uint32_t min_max = ~0;

      for (unsigned i = 0; i < ctx->streamout.num_targets; ++i) {
         struct pipe_stream_output_target *target = get_target(ctx, i);

         if (!target)
            continue;

         struct pipe_stream_output_info *so =
            &ctx->stage[PIPE_SHADER_VERTEX].shader->base.stream_output;
         unsigned stride = so->stride[i] * 4;

         /* Ignore spurious targets. I don't see anything in the Gallium
          * contract specifically forbidding this.
          */
         if (stride == 0)
            continue;

         uint32_t offset = agx_so_target(target)->offset * stride;
         uint32_t remaining =
            offset < target->buffer_size ? (target->buffer_size - offset) : 0;
         uint32_t max_vertices = stride ? (remaining / stride) : ~0;

         min_max = MIN2(min_max, max_vertices);
      }

      /* We now have the maximum vertices written, round down to primitives */
      uint32_t max_prims = xfb_prims_for_vertices(info->mode, min_max);
      uint32_t prims = xfb_prims_for_vertices(info->mode, draw->count);

      ctx->tf_prims_generated->value += MIN2(prims, max_prims);
   }

   /* Update the offsets into the streamout buffers */
   for (unsigned i = 0; i < ctx->streamout.num_targets; ++i) {
      if (ctx->streamout.targets[i])
         agx_so_target(ctx->streamout.targets[i])->offset += num_outputs;
   }

   ctx->dirty |= AGX_DIRTY_XFB;
   ctx->streamout.key.active = false;
}

/*
 * Count generated primitives on the CPU for transform feedback. This only works
 * in the absence of indirect draws, geometry shaders, or tessellation.
 */
void
agx_primitives_update_direct(struct agx_context *ctx,
                             const struct pipe_draw_info *info,
                             const struct pipe_draw_start_count_bias *draw)
{
   assert(ctx->active_queries && ctx->prims_generated && "precondition");

   ctx->prims_generated->value +=
      xfb_prims_for_vertices(info->mode, draw->count);
}

/* The OpenGL spec says:
 *
 *    If recording the vertices of a primitive to the buffer objects being
 *    used for transform feedback purposes would result in either exceeding
 *    the limits of any buffer object’s size, or in exceeding the end
 *    position offset + size − 1, as set by BindBufferRange, then no vertices
 *    of that primitive are recorded in any buffer object.
 *
 * This function checks for the absence of overflow.
 *
 * The difficulty is that we are processing a single vertex at a time, so we
 * need to do some arithmetic to figure out the bounds for the whole containing
 * primitive.
 *
 * XXX: How do quads get tessellated?
 */
static nir_def *
primitive_fits(nir_builder *b, struct agx_xfb_key *key)
{
   /* Get the number of vertices per primitive in the current mode, usually just
    * the base number but quads are tessellated.
    */
   uint32_t verts_per_prim = u_vertices_per_prim(key->mode);

   if (u_decomposed_prim(key->mode) == MESA_PRIM_QUADS)
      verts_per_prim = 6;

   /* Get the ID for this invocation */
   nir_def *id = nir_load_vertex_id_zero_base(b);

   /* Figure out the ID for the first vertex of the next primitive. Since
    * transform feedback buffers are tightly packed, that's one byte after the
    * end of this primitive, which will make bounds checking convenient. That
    * will be:
    *
    *    (id - (id % prim size)) + prim size
    */
   nir_def *rem = nir_umod_imm(b, id, verts_per_prim);
   nir_def *next_id = nir_iadd_imm(b, nir_isub(b, id, rem), verts_per_prim);

   /* Figure out where that vertex will land */
   nir_def *index = nir_iadd(
      b, nir_imul(b, nir_load_instance_id(b), nir_load_num_vertices(b)),
      next_id);

   /* Now check for overflow in each written buffer */
   nir_def *all_fits = nir_imm_true(b);

   u_foreach_bit(buffer, b->shader->xfb_info->buffers_written) {
      uint16_t stride = b->shader->info.xfb_stride[buffer] * 4;
      assert(stride != 0);

      /* For this primitive to fit, the next primitive cannot start after the
       * end of the transform feedback buffer.
       */
      nir_def *end_offset = nir_imul_imm(b, index, stride);

      /* Check whether that will remain in bounds */
      nir_def *fits =
         nir_uge(b, nir_load_xfb_size(b, .base = buffer), end_offset);

      /* Accumulate */
      all_fits = nir_iand(b, all_fits, fits);
   }

   return all_fits;
}

static void
insert_overflow_check(nir_shader *nir, struct agx_xfb_key *key)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);

   /* Extract the current transform feedback shader */
   nir_cf_list list;
   nir_cf_extract(&list, nir_before_impl(impl), nir_after_impl(impl));

   /* Get a builder for the (now empty) shader */
   nir_builder b = nir_builder_at(nir_after_block(nir_start_block(impl)));

   /* Rebuild the shader as
    *
    *    if (!overflow) {
    *       shader();
    *    }
    */
   nir_push_if(&b, primitive_fits(&b, key));
   {
      b.cursor = nir_cf_reinsert(&list, b.cursor);
   }
   nir_pop_if(&b, NULL);
}

static void
lower_xfb_output(nir_builder *b, nir_intrinsic_instr *intr,
                 unsigned start_component, unsigned num_components,
                 unsigned buffer, unsigned offset_words)
{
   assert(buffer < MAX_XFB_BUFFERS);
   assert(nir_intrinsic_component(intr) == 0); // TODO

   /* Transform feedback info in units of words, convert to bytes. */
   uint16_t stride = b->shader->info.xfb_stride[buffer] * 4;
   assert(stride != 0);

   uint16_t offset = offset_words * 4;

   nir_def *index = nir_iadd(
      b, nir_imul(b, nir_load_instance_id(b), nir_load_num_vertices(b)),
      nir_load_vertex_id_zero_base(b));

   nir_def *xfb_offset =
      nir_iadd_imm(b, nir_imul_imm(b, index, stride), offset);

   nir_def *buf = nir_load_xfb_address(b, 64, .base = buffer);
   nir_def *addr = nir_iadd(b, buf, nir_u2u64(b, xfb_offset));

   nir_def *value = nir_channels(
      b, intr->src[0].ssa, BITFIELD_MASK(num_components) << start_component);
   nir_store_global(b, addr, 4, value, nir_component_mask(num_components));
}

static bool
lower_xfb(nir_builder *b, nir_intrinsic_instr *intr, UNUSED void *data)
{
   if (intr->intrinsic != nir_intrinsic_store_output)
      return false;

   /* Assume the inputs are read */
   BITSET_SET(b->shader->info.system_values_read,
              SYSTEM_VALUE_VERTEX_ID_ZERO_BASE);
   BITSET_SET(b->shader->info.system_values_read, SYSTEM_VALUE_INSTANCE_ID);

   bool progress = false;

   for (unsigned i = 0; i < 2; ++i) {
      nir_io_xfb xfb =
         i ? nir_intrinsic_io_xfb2(intr) : nir_intrinsic_io_xfb(intr);

      for (unsigned j = 0; j < 2; ++j) {
         if (xfb.out[j].num_components > 0) {
            b->cursor = nir_before_instr(&intr->instr);
            lower_xfb_output(b, intr, i * 2 + j, xfb.out[j].num_components,
                             xfb.out[j].buffer, xfb.out[j].offset);
            progress = true;
         }
      }
   }

   nir_instr_remove(&intr->instr);
   return progress;
}

static bool
lower_xfb_intrinsics(struct nir_builder *b, nir_intrinsic_instr *intr,
                     void *data)
{
   b->cursor = nir_before_instr(&intr->instr);

   struct agx_xfb_key *key = data;

   switch (intr->intrinsic) {
   /* XXX: Rename to "xfb index" to avoid the clash */
   case nir_intrinsic_load_vertex_id_zero_base: {
      nir_def *id = nir_load_vertex_id(b);
      nir_def_rewrite_uses(&intr->def, id);
      return true;
   }

   case nir_intrinsic_load_vertex_id: {
      /* Get the raw invocation ID */
      nir_def *id = nir_load_vertex_id(b);

      /* Tessellate by primitive mode */
      if (key->mode == MESA_PRIM_LINE_STRIP ||
          key->mode == MESA_PRIM_LINE_LOOP) {
         /* The last vertex is special for a loop. Check if that's we're dealing
          * with.
          */
         nir_def *num_invocations =
            nir_imul_imm(b, nir_load_num_vertices(b), 2);
         nir_def *last_vertex =
            nir_ieq(b, id, nir_iadd_imm(b, num_invocations, -1));

         /* (0, 1), (1, 2) */
         id = nir_iadd(b, nir_ushr_imm(b, id, 1), nir_iand_imm(b, id, 1));

         /* (0, 1), (1, 2), (2, 0) */
         if (key->mode == MESA_PRIM_LINE_LOOP) {
            id = nir_bcsel(b, last_vertex, nir_imm_int(b, 0), id);
         }
      } else if (key->mode == MESA_PRIM_TRIANGLE_STRIP) {
         /* Order depends on the provoking vertex.
          *
          * First: (0, 1, 2), (1, 3, 2), (2, 3, 4).
          * Last:  (0, 1, 2), (2, 1, 3), (2, 3, 4).
          */
         nir_def *prim = nir_udiv_imm(b, id, 3);
         nir_def *rem = nir_umod_imm(b, id, 3);

         unsigned pv = key->flatshade_first ? 0 : 2;

         /* Swap the two non-provoking vertices third vertex in odd triangles */
         nir_def *even = nir_ieq_imm(b, nir_iand_imm(b, prim, 1), 0);
         nir_def *is_provoking = nir_ieq_imm(b, rem, pv);
         nir_def *no_swap = nir_ior(b, is_provoking, even);
         nir_def *swapped = nir_isub_imm(b, 3 - pv, rem);
         nir_def *off = nir_bcsel(b, no_swap, rem, swapped);

         /* Pull the (maybe swapped) vertex from the corresponding primitive */
         id = nir_iadd(b, prim, off);
      } else if (key->mode == MESA_PRIM_TRIANGLE_FAN) {
         /* (0, 1, 2), (0, 2, 3) */
         nir_def *prim = nir_udiv_imm(b, id, 3);
         nir_def *rem = nir_umod_imm(b, id, 3);

         id = nir_bcsel(b, nir_ieq_imm(b, rem, 0), nir_imm_int(b, 0),
                        nir_iadd(b, prim, rem));
      } else if (key->mode == MESA_PRIM_QUADS ||
                 key->mode == MESA_PRIM_QUAD_STRIP) {
         /* Quads:       [(0, 1, 3), (3, 1, 2)], [(4, 5, 7), (7, 5, 6)]
          * Quad strips: [(0, 1, 3), (0, 2, 3)], [(2, 3, 5), (2, 4, 5)]
          */
         bool strips = key->mode == MESA_PRIM_QUAD_STRIP;

         nir_def *prim = nir_udiv_imm(b, id, 6);
         nir_def *rem = nir_umod_imm(b, id, 6);
         nir_def *base = nir_imul_imm(b, prim, strips ? 2 : 4);

         /* Quads:       [0, 1, 3, 3, 1, 2]
          * Quad strips: [0, 1, 3, 0, 2, 3]
          */
         uint32_t order_quads = 0x213310;
         uint32_t order_strips = 0x230310;
         uint32_t order = strips ? order_strips : order_quads;

         /* Index out of the bitpacked array */
         nir_def *offset = nir_iand_imm(
            b, nir_ushr(b, nir_imm_int(b, order), nir_imul_imm(b, rem, 4)),
            0xF);

         id = nir_iadd(b, base, offset);
      }

      /* Add the "start", either an index bias or a base vertex */
      id = nir_iadd(b, id, nir_load_base_vertex(b));

      /* If drawing with an index buffer, pull the vertex ID. Otherwise, the
       * vertex ID is just the index as-is.
       */
      if (key->index_size) {
         nir_def *index_buffer = nir_load_xfb_index_buffer(b, 64);
         nir_def *offset = nir_imul_imm(b, id, key->index_size);
         nir_def *address = nir_iadd(b, index_buffer, nir_u2u64(b, offset));
         nir_def *index = nir_load_global_constant(b, address, key->index_size,
                                                   1, key->index_size * 8);

         id = nir_u2uN(b, index, id->bit_size);
      }

      nir_def_rewrite_uses(&intr->def, id);
      return true;
   }

   default:
      return false;
   }
}

void
agx_nir_lower_xfb(nir_shader *nir, struct agx_xfb_key *key)
{
   assert(nir->info.stage == MESA_SHADER_VERTEX);

   NIR_PASS_V(nir, nir_io_add_const_offset_to_base,
              nir_var_shader_in | nir_var_shader_out);
   NIR_PASS_V(nir, nir_io_add_intrinsic_xfb_info);

   NIR_PASS_V(nir, insert_overflow_check, key);
   NIR_PASS_V(nir, nir_shader_intrinsics_pass, lower_xfb,
              nir_metadata_block_index | nir_metadata_dominance, key);
   NIR_PASS_V(nir, nir_shader_intrinsics_pass, lower_xfb_intrinsics,
              nir_metadata_block_index | nir_metadata_dominance, key);

   /* Lowering XFB creates piles of dead code. Eliminate now so we don't
    * push unnecessary sysvals.
    */
   NIR_PASS_V(nir, nir_opt_dce);
}

void
agx_init_streamout_functions(struct pipe_context *ctx)
{
   ctx->create_stream_output_target = agx_create_stream_output_target;
   ctx->stream_output_target_destroy = agx_stream_output_target_destroy;
   ctx->set_stream_output_targets = agx_set_stream_output_targets;
}
