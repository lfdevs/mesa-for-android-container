/*
 * Copyright 2023 Alyssa Rosenzweig
 * Copyright 2023 Valve Corporation
 * Copyright 2015 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "poly/nir/poly_nir.h"
#include "compiler/nir/nir_builder.h"
#include "gallium/include/pipe/p_defines.h"
#include "poly/cl/libpoly.h"
#include "poly/geometry.h"
#include "util/bitscan.h"
#include "util/list.h"
#include "util/macros.h"
#include "util/ralloc.h"
#include "util/u_math.h"
#include "nir.h"
#include "nir_builder_opcodes.h"
#include "nir_intrinsics.h"
#include "nir_intrinsics_indices.h"
#include "nir_xfb_info.h"
#include "shader_enums.h"

struct state {
   nir_variable *vertices[NIR_MAX_XFB_STREAMS];
   nir_variable *first_vertex[NIR_MAX_XFB_STREAMS];
   nir_variable *xfb_count[NIR_MAX_XFB_STREAMS];
   nir_variable *indices;
};

static void
emit_primitive(nir_builder *b, struct state *state, unsigned stream)
{
   unsigned min_verts = nir_verts_in_output_prim(b->shader);
   bool restart = min_verts > 1;

   nir_def *indices = nir_load_var(b, state->indices);
   nir_def *first_vertex = nir_load_var(b, state->first_vertex[stream]);
   nir_def *total_vertices = nir_load_var(b, state->vertices[stream]);
   nir_def *xfb_count = nir_load_var(b, state->xfb_count[stream]);
   nir_def *length = nir_isub(b, total_vertices, first_vertex);

   nir_emit_primitive_poly(b, indices, first_vertex, length, xfb_count, stream);

   /* Allocate index buffer space */
   nir_def *degenerate = nir_ult_imm(b, length, min_verts);
   nir_def *added_indices = nir_iadd_imm(b, length, restart);
   added_indices = nir_bcsel(b, degenerate, nir_imm_int(b, 0), added_indices);
   nir_store_var(b, state->indices, nir_iadd(b, indices, added_indices), 0x1);

   /* We form a new primitive for every vertex emitted after the first
    * complete primitive (since we're outputting strips).
    */
   nir_def *xfb_prims = nir_iadd_imm(b, length, -(min_verts - 1));
   xfb_prims = nir_bcsel(b, degenerate, nir_imm_int(b, 0), xfb_prims);
   nir_store_var(b, state->xfb_count[stream], nir_iadd(b, xfb_count, xfb_prims),
                 0x1);

   nir_store_var(b, state->first_vertex[stream], total_vertices, 0x1);
}

static bool
rewrite_intrinsics(nir_builder *b, nir_intrinsic_instr *intr, void *state_)
{
   b->cursor = nir_before_instr(&intr->instr);
   struct state *state = state_;

   if (intr->intrinsic == nir_intrinsic_emit_vertex) {
      unsigned stream = nir_intrinsic_stream_id(intr);

      nir_def *count = nir_load_var(b, state->vertices[stream]);
      nir_select_vertex_poly(b, count, stream);
      nir_store_var(b, state->vertices[stream], nir_iadd_imm(b, count, 1), 0x1);
   } else if (intr->intrinsic == nir_intrinsic_end_primitive) {
      /* Emit is deferred for points */
      if (b->shader->info.gs.output_primitive != MESA_PRIM_POINTS)
         emit_primitive(b, state, nir_intrinsic_stream_id(intr));
   } else {
      return false;
   }

   nir_instr_remove(&intr->instr);
   return true;
}

static bool
lower_gs_intrinsics(nir_shader *shader)
{
   struct state state;
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);
   nir_builder b = nir_builder_at(nir_before_impl(impl));
   nir_def *zero = nir_imm_int(&b, 0);
   const glsl_type *T = glsl_uint_type();

   for (unsigned i = 0; i < NIR_MAX_XFB_STREAMS; ++i) {
      state.vertices[i] = nir_local_variable_create(impl, T, NULL);
      state.first_vertex[i] = nir_local_variable_create(impl, T, NULL);
      state.xfb_count[i] = nir_local_variable_create(impl, T, NULL);

      nir_store_var(&b, state.vertices[i], zero, 0x1);
      nir_store_var(&b, state.first_vertex[i], zero, 0x1);
      nir_store_var(&b, state.xfb_count[i], zero, 0x1);
   }

   state.indices = nir_local_variable_create(impl, T, NULL);
   nir_store_var(&b, state.indices, zero, 0x1);

   /* Make sure all the primitives are ended at the end of the shader. */
   b.cursor = nir_after_impl(impl);

   u_foreach_bit(stream, shader->info.gs.active_stream_mask) {
      nir_end_primitive(&b, stream);
   }

   nir_shader_intrinsics_pass(shader, rewrite_intrinsics,
                              nir_metadata_control_flow, &state);

   b.cursor = nir_after_impl(impl);

   if (shader->info.gs.output_primitive == MESA_PRIM_POINTS) {
      u_foreach_bit(stream, shader->info.gs.active_stream_mask) {
         emit_primitive(&b, &state, stream);
      }
   }

   /* If we have side effects, make sure we run the geometry shader at least
    * once by outputting a dummy primitive if we wouldn't output anything.
    */
   if (shader->info.writes_memory) {
      unsigned n = nir_verts_in_output_prim(shader);
      shader->info.gs.vertices_out = MAX2(shader->info.gs.vertices_out, n);

      nir_push_if(&b, nir_ieq_imm(&b, nir_load_var(&b, state.indices), 0));
      {
         nir_def *zero = nir_imm_int(&b, 0);
         nir_def *n_ = nir_imm_int(&b, n);
         bool restart = n > 1;

         shader->info.outputs_written |= VARYING_BIT_POS;
         nir_store_output(&b, nir_imm_float(&b, NAN), zero,
                          .io_semantics.location = VARYING_SLOT_POS);
         nir_select_vertex_poly(&b, zero);
         nir_emit_primitive_poly(&b, zero, zero, n_, zero);
         nir_store_var(&b, state.indices, nir_iadd_imm(&b, n_, restart), 1);
      }
      nir_pop_if(&b, NULL);
   }

   /* Report the counts */
   for (unsigned stream = 0; stream < NIR_MAX_XFB_STREAMS; ++stream) {
      nir_set_vertex_and_primitive_count(
         &b, nir_imm_int(&b, 0), nir_load_var(&b, state.indices),
         nir_load_var(&b, state.xfb_count[stream]), stream);
   }

   return nir_progress(true, impl, nir_metadata_none);
}

struct lower_gs_state {
   int static_count[POLY_MAX_VERTEX_STREAMS];

   /* The index of each counter in the count buffer, or -1 if it's not in the
    * count buffer.
    *
    * Invariant: info->count_words == sum(count_index[i] >= 0).
    */
   int count_index[POLY_MAX_VERTEX_STREAMS];

   struct poly_gs_info *info;
};

/* Helpers for loading from the vertex state buffer */
static nir_def *
load_vertex_param_offset(nir_builder *b, uint32_t offset, uint8_t bytes)
{
   nir_def *base = nir_load_vertex_param_buffer_poly(b);
   nir_def *addr = nir_iadd_imm(b, base, offset);

   assert((offset % bytes) == 0 && "must be naturally aligned");

   return nir_load_global_constant(b, 1, bytes * 8, addr);
}

#define load_vertex_param(b, field)                                            \
   load_vertex_param_offset(                                                   \
      b, offsetof(struct poly_vertex_params, field),                           \
      sizeof(((struct poly_vertex_params *)0)->field))

/* Helpers for loading from the geometry state buffer */
static nir_def *
load_geometry_param_offset(nir_builder *b, uint32_t offset, uint8_t bytes)
{
   nir_def *base = nir_load_geometry_param_buffer_poly(b);
   nir_def *addr = nir_iadd_imm(b, base, offset);

   assert((offset % bytes) == 0 && "must be naturally aligned");

   return nir_load_global_constant(b, 1, bytes * 8, addr);
}

#define load_geometry_param(b, field)                                          \
   load_geometry_param_offset(                                                 \
      b, offsetof(struct poly_geometry_params, field),                         \
      sizeof(((struct poly_geometry_params *)0)->field))

/* Helpers for lowering I/O to variables */
struct lower_output_to_var_state {
   nir_variable *outputs[NUM_TOTAL_VARYING_SLOTS];
};

static void
lower_store_to_var(nir_builder *b, nir_intrinsic_instr *intr,
                   struct lower_output_to_var_state *state)
{
   b->cursor = nir_instr_remove(&intr->instr);
   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   unsigned component = nir_intrinsic_component(intr);
   nir_def *value = intr->src[0].ssa;

   assert(nir_src_is_const(intr->src[1]) && "no indirect outputs");
   assert(nir_intrinsic_write_mask(intr) == nir_component_mask(1) &&
          "should be scalarized");

   nir_variable *var =
      state->outputs[sem.location + nir_src_as_uint(intr->src[1])];
   if (!var) {
      assert(sem.location == VARYING_SLOT_PSIZ &&
             "otherwise in outputs_written");
      return;
   }

   unsigned nr_components = glsl_get_components(glsl_without_array(var->type));
   assert(component < nr_components);

   /* Turn it into a vec4 write like NIR expects */
   value = nir_vector_insert_imm(b, nir_undef(b, nr_components, 32), value,
                                 component);

   nir_store_var(b, var, value, BITFIELD_BIT(component));
}

/*
 * Geometry shader invocations are compute-like:
 *
 * (primitive ID, instance ID, 1)
 */
static nir_def *
load_primitive_id(nir_builder *b)
{
   return nir_channel(b, nir_load_global_invocation_id(b, 32), 0);
}

static nir_def *
load_instance_id(nir_builder *b)
{
   return nir_channel(b, nir_load_global_invocation_id(b, 32), 1);
}

/* Geometry shaders use software input assembly. The software vertex shader
 * is invoked for each index, and the geometry shader applies the topology. This
 * helper applies the topology.
 */
static nir_def *
vertex_id_for_topology_class(nir_builder *b, nir_def *vert, enum mesa_prim cls)
{
   nir_def *prim = nir_load_primitive_id(b);
   nir_def *flatshade_first = nir_ieq_imm(b, nir_load_provoking_last(b), 0);
   nir_def *nr = load_geometry_param(b, grid[0]);
   nir_def *topology = nir_load_input_topology_poly(b);

   switch (cls) {
   case MESA_PRIM_POINTS:
      return prim;

   case MESA_PRIM_LINES:
      return poly_vertex_id_for_line_class(b, topology, prim, vert, nr);

   case MESA_PRIM_TRIANGLES:
      return poly_vertex_id_for_tri_class(b, topology, prim, vert,
                                          flatshade_first);

   case MESA_PRIM_LINES_ADJACENCY:
      return poly_vertex_id_for_line_adj_class(b, topology, prim, vert);

   case MESA_PRIM_TRIANGLES_ADJACENCY:
      return poly_vertex_id_for_tri_adj_class(b, topology, prim, vert, nr,
                                              flatshade_first);

   default:
      UNREACHABLE("invalid topology class");
   }
}

nir_def *
poly_load_per_vertex_input(nir_builder *b, nir_intrinsic_instr *intr,
                           nir_def *vertex)
{
   assert(intr->intrinsic == nir_intrinsic_load_per_vertex_input);
   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);

   nir_def *location = nir_iadd_imm(b, intr->src[1].ssa, sem.location);
   nir_def *addr;

   nir_def *vp = nir_load_vertex_param_buffer_poly(b);

   nir_def *input_mask;
   if (b->shader->info.stage == MESA_SHADER_GEOMETRY) {
      /* GS may be preceded by VS or TES so specified as param */
      input_mask = poly_vertex_outputs(b, vp);
   } else {
      assert(b->shader->info.stage == MESA_SHADER_TESS_CTRL);

      /* TCS always preceded by VS so we use the VS state directly */
      input_mask = nir_load_vs_outputs_poly(b);
   }

   addr = poly_vertex_output_address(b, vp, input_mask, vertex, location);

   addr = nir_iadd_imm(b, addr, 4 * nir_intrinsic_component(intr));
   return nir_load_global_constant(b, intr->def.num_components,
                                   intr->def.bit_size, addr, .align_mul = 4);
}

static bool
lower_gs_inputs(nir_builder *b, nir_intrinsic_instr *intr, void *_)
{
   if (intr->intrinsic != nir_intrinsic_load_per_vertex_input)
      return false;

   b->cursor = nir_before_instr(&intr->instr);

   /* Calculate the vertex ID we're pulling, based on the topology class */
   nir_def *vert_in_prim = intr->src[0].ssa;
   nir_def *vertex = vertex_id_for_topology_class(
      b, vert_in_prim, b->shader->info.gs.input_primitive);

   nir_def *verts = load_vertex_param(b, grid[0]);
   nir_def *unrolled =
      nir_iadd(b, nir_imul(b, nir_load_instance_id(b), verts), vertex);

   nir_def *val = poly_load_per_vertex_input(b, intr, unrolled);
   nir_def_replace(&intr->def, val);
   return true;
}

/*
 * Unrolled ID is the index of the primitive in the count buffer, given as
 * (instance ID * # vertices/instance) + vertex ID
 */
static nir_def *
calc_unrolled_id(nir_builder *b)
{
   return nir_iadd(
      b, nir_imul(b, load_instance_id(b), load_geometry_param(b, grid[0])),
      load_primitive_id(b));
}

static unsigned
output_vertex_id_pot_stride(const nir_shader *gs)
{
   return util_next_power_of_two(gs->info.gs.vertices_out);
}

/* Variant of calc_unrolled_id that uses a power-of-two stride for indices. This
 * is sparser (acceptable for index buffer values, not for count buffer
 * indices). It has the nice property of being cheap to invert, unlike
 * calc_unrolled_id. So, we use calc_unrolled_id for count buffers and
 * calc_unrolled_index_id for index values.
 *
 * This also multiplies by the appropriate stride to calculate the final index
 * base value.
 */
static nir_def *
calc_unrolled_index_id(nir_builder *b)
{
   /* We know this is a dynamic topology and hence indexed */
   unsigned vertex_stride = output_vertex_id_pot_stride(b->shader);
   nir_def *primitives_log2 = load_geometry_param(b, primitives_log2);

   nir_def *instance = nir_ishl(b, load_instance_id(b), primitives_log2);
   nir_def *prim = nir_iadd(b, instance, load_primitive_id(b));

   return nir_imul_imm(b, prim, vertex_stride);
}

static void
write_xfb_counts(nir_builder *b, nir_intrinsic_instr *intr,
                 struct lower_gs_state *state)
{
   unsigned stream = nir_intrinsic_stream_id(intr);
   if (state->count_index[stream] < 0)
      return;

   /* Store each required counter */
   nir_def *id =
      state->info->prefix_sum ? calc_unrolled_id(b) : nir_imm_int(b, 0);

   nir_def *addr =
      poly_load_xfb_count_address(b, nir_load_geometry_param_buffer_poly(b),
                                  nir_imm_int(b, state->count_index[stream]),
                                  nir_imm_int(b, state->info->count_words), id);

   if (state->info->prefix_sum) {
      nir_store_global(b, intr->src[2].ssa, addr);
   } else {
      nir_global_atomic(b, 32, addr, intr->src[2].ssa,
                        .atomic_op = nir_atomic_op_iadd);
   }
}

static bool
lower_gs_count_instr(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   switch (intr->intrinsic) {
   case nir_intrinsic_store_output:
   case nir_intrinsic_select_vertex_poly:
   case nir_intrinsic_emit_primitive_poly:
      /* These are for the main shader, just remove them */
      nir_instr_remove(&intr->instr);
      return true;

   case nir_intrinsic_set_vertex_and_primitive_count:
      b->cursor = nir_instr_remove(&intr->instr);
      write_xfb_counts(b, intr, data);
      return true;

   default:
      return false;
   }
}

static bool
lower_id(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   b->cursor = nir_before_instr(&intr->instr);

   nir_def *id;
   if (intr->intrinsic == nir_intrinsic_load_primitive_id)
      id = load_primitive_id(b);
   else if (intr->intrinsic == nir_intrinsic_load_instance_id)
      id = load_instance_id(b);
   else if (intr->intrinsic == nir_intrinsic_load_flat_mask)
      id = load_geometry_param(b, flat_outputs);
   else if (intr->intrinsic == nir_intrinsic_load_input_topology_poly)
      id = load_geometry_param(b, input_topology);
   else
      return false;

   nir_def_replace(&intr->def, id);
   return true;
}

/*
 * Create a "Geometry count" shader. This is a stripped down geometry shader
 * that just write its number of emitted vertices / primitives / transform
 * feedback primitives to a count buffer. That count buffer will be prefix
 * summed prior to running the real geometry shader. This is skipped if the
 * counts are statically known.
 */
static nir_shader *
create_geometry_count_shader(nir_shader *gs, struct lower_gs_state *state)
{
   /* Don't muck up the original shader */
   nir_shader *shader = nir_shader_clone(NULL, gs);

   if (shader->info.name) {
      shader->info.name =
         ralloc_asprintf(shader, "%s_count", shader->info.name);
   } else {
      shader->info.name = "count";
   }

   NIR_PASS(_, shader, nir_shader_intrinsics_pass, lower_gs_count_instr,
            nir_metadata_control_flow, state);

   NIR_PASS(_, shader, nir_shader_intrinsics_pass, lower_id,
            nir_metadata_control_flow, NULL);

   return shader;
}

struct lower_gs_rast_state {
   nir_def *raw_instance_id;
   nir_def *instance_id, *primitive_id, *output_id, *stream;
   struct lower_output_to_var_state outputs;
   struct lower_output_to_var_state selected;
   bool points;

   nir_variable *output_strip_length, *output_strip_base, *id_in_strip;
};

static void
select_rast_output(nir_builder *b, nir_intrinsic_instr *intr,
                   struct lower_gs_rast_state *state)
{
   b->cursor = nir_instr_remove(&intr->instr);
   nir_def *us = nir_ieq(b, intr->src[0].ssa, state->output_id);
   us = nir_iand(b, us,
                 nir_ieq_imm(b, state->stream, nir_intrinsic_stream_id(intr)));

   u_foreach_bit64(slot, b->shader->info.outputs_written) {
      nir_def *orig = nir_load_var(b, state->selected.outputs[slot]);
      nir_def *data = nir_load_var(b, state->outputs.outputs[slot]);

      nir_def *value = nir_bcsel(b, us, data, orig);

      nir_store_var(b, state->selected.outputs[slot], value,
                    nir_component_mask(value->num_components));
   }
}

static bool
lower_to_gs_rast(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   struct lower_gs_rast_state *state = data;

   switch (intr->intrinsic) {
   case nir_intrinsic_store_output:
      lower_store_to_var(b, intr, &state->outputs);
      return true;

   case nir_intrinsic_select_vertex_poly:
      select_rast_output(b, intr, state);
      return true;

   case nir_intrinsic_load_primitive_id:
      nir_def_replace(&intr->def, state->primitive_id);
      return true;

   case nir_intrinsic_load_instance_id:
      /* Don't lower recursively */
      if (state->raw_instance_id == &intr->def)
         return false;

      nir_def_replace(&intr->def, state->instance_id);
      return true;

   case nir_intrinsic_load_flat_mask:
   case nir_intrinsic_load_provoking_last:
   case nir_intrinsic_load_input_topology_poly: {
      /* Lowering the same in both GS variants */
      return lower_id(b, intr, NULL);
   }

   case nir_intrinsic_emit_primitive_poly: {
      b->cursor = nir_before_instr(&intr->instr);
      nir_def *id = state->output_id;

      nir_def *first_id = intr->src[1].ssa;
      nir_def *length = intr->src[2].ssa;
      nir_def *base = intr->src[3].ssa;
      nir_def *id_in_strip = nir_isub(b, id, first_id);

      nir_def *us = nir_ult(b, id, nir_iadd(b, first_id, length));
      us = nir_iand(b, us, nir_uge(b, id, first_id));
      us = nir_iand(
         b, us, nir_ieq_imm(b, state->stream, nir_intrinsic_stream_id(intr)));

      nir_def *orig = nir_load_var(b, state->output_strip_length);
      nir_def *value = nir_bcsel(b, us, length, orig);
      nir_store_var(b, state->output_strip_length, value,
                    nir_component_mask(1));

      orig = nir_load_var(b, state->output_strip_base);
      value = nir_bcsel(b, us, base, orig);
      nir_store_var(b, state->output_strip_base, value, nir_component_mask(1));

      orig = nir_load_var(b, state->id_in_strip);
      value = nir_bcsel(b, us, id_in_strip, orig);
      nir_store_var(b, state->id_in_strip, value, nir_component_mask(1));

      nir_instr_remove(&intr->instr);
      return true;
   }

   case nir_intrinsic_set_vertex_and_primitive_count:
      nir_instr_remove(&intr->instr);
      return true;

   default:
      return false;
   }
}

static bool
strip_side_effect_from_main(nir_builder *b, nir_intrinsic_instr *intr,
                            void *data)
{
   switch (intr->intrinsic) {
   case nir_intrinsic_global_atomic:
   case nir_intrinsic_global_atomic_swap:
   case nir_intrinsic_image_atomic:
   case nir_intrinsic_image_atomic_swap:
   case nir_intrinsic_bindless_image_atomic:
   case nir_intrinsic_bindless_image_atomic_swap:
      if (list_is_empty(&intr->def.uses)) {
         nir_instr_remove(&intr->instr);
         return true;
      }

      return false;

   case nir_intrinsic_store_global:
   case nir_intrinsic_image_store:
   case nir_intrinsic_bindless_image_store:
   case nir_intrinsic_fence_pbe_to_tex_agx:
      if (data) {
         nir_instr_remove(&intr->instr);
         return true;
      }

      return false;

   default:
      return false;
   }
}

/*
 * The stream # is encoded into the lower bits of an index. The stream
 * multiplier is the factor to multiply vertex IDs before adding the stream #.
 */
static unsigned
stream_multiplier(const nir_shader *gs)
{
   unsigned nr_streams = util_last_bit(gs->info.gs.active_stream_mask);
   return util_next_power_of_two(nr_streams);
}

/*
 * Create a GS rasterization shader. This is a hardware vertex shader that
 * shades each rasterized output vertex in parallel.
 */
static nir_shader *
create_gs_rast_shader(const nir_shader *gs, const struct lower_gs_state *state)
{
   /* Don't muck up the original shader */
   nir_shader *shader = nir_shader_clone(NULL, gs);

   /* Turn into a vertex shader run only for rasterization. Transform feedback
    * was handled in the prepass.
    */
   shader->info.stage = MESA_SHADER_VERTEX;
   shader->info.has_transform_feedback_varyings = false;
   memset(&shader->info.vs, 0, sizeof(shader->info.vs));
   shader->xfb_info = NULL;

   if (shader->info.name) {
      shader->info.name = ralloc_asprintf(shader, "%s_rast", shader->info.name);
   } else {
      shader->info.name = "gs rast";
   }

   /* Optimize out pointless gl_PointSize outputs. Bizarrely, these occur. */
   if (shader->info.gs.output_primitive != MESA_PRIM_POINTS)
      shader->info.outputs_written &= ~VARYING_BIT_PSIZ;

   nir_builder b_ =
      nir_builder_at(nir_before_impl(nir_shader_get_entrypoint(shader)));
   nir_builder *b = &b_;

   const glsl_type *T = glsl_uint_type();
   nir_def *raw_vertex_id = nir_load_vertex_id(b);

   struct lower_gs_rast_state rs = {
      .raw_instance_id = nir_load_instance_id(b),
      .points = gs->info.gs.output_primitive == MESA_PRIM_POINTS,
      .stream = nir_umod_imm(b, raw_vertex_id, stream_multiplier(gs)),
      .output_strip_length = nir_local_variable_create(b->impl, T, NULL),
      .output_strip_base = nir_local_variable_create(b->impl, T, NULL),
      .id_in_strip = nir_local_variable_create(b->impl, T, NULL),
   };

   raw_vertex_id = nir_udiv_imm(b, raw_vertex_id, stream_multiplier(gs));

   switch (state->info->shape) {
   case POLY_GS_SHAPE_DYNAMIC_INDEXED: {
      unsigned stride = output_vertex_id_pot_stride(gs);

      nir_def *unrolled = nir_udiv_imm(b, raw_vertex_id, stride);
      nir_def *primitives_log2 = load_geometry_param(b, primitives_log2);
      nir_def *bit = nir_ishl(b, nir_imm_int(b, 1), primitives_log2);

      rs.output_id = nir_umod_imm(b, raw_vertex_id, stride);
      rs.instance_id = nir_ushr(b, unrolled, primitives_log2);
      rs.primitive_id = nir_iand(b, unrolled, nir_iadd_imm(b, bit, -1));
      break;
   }

   case POLY_GS_SHAPE_STATIC_INDEXED:
   case POLY_GS_SHAPE_STATIC_PER_PRIM: {
      nir_def *stride = load_geometry_param(b, grid[0]);

      rs.output_id = raw_vertex_id;
      rs.instance_id = nir_udiv(b, rs.raw_instance_id, stride);
      rs.primitive_id = nir_umod(b, rs.raw_instance_id, stride);
      break;
   }

   case POLY_GS_SHAPE_STATIC_PER_INSTANCE: {
      unsigned stride = MAX2(state->info->max_indices, 1);

      rs.output_id = nir_umod_imm(b, raw_vertex_id, stride);
      rs.primitive_id = nir_udiv_imm(b, raw_vertex_id, stride);
      rs.instance_id = rs.raw_instance_id;
      break;
   }

   default:
      UNREACHABLE("invalid shape");
   }

   u_foreach_bit64(slot, shader->info.outputs_written) {
      const char *slot_name =
         gl_varying_slot_name_for_stage(slot, MESA_SHADER_GEOMETRY);

      bool scalar = (slot == VARYING_SLOT_PSIZ) ||
                    (slot == VARYING_SLOT_LAYER) ||
                    (slot == VARYING_SLOT_VIEWPORT);
      unsigned comps = scalar ? 1 : 4;

      rs.outputs.outputs[slot] = nir_variable_create(
         shader, nir_var_shader_temp, glsl_vector_type(GLSL_TYPE_UINT, comps),
         ralloc_asprintf(shader, "%s-temp", slot_name));

      rs.selected.outputs[slot] = nir_variable_create(
         shader, nir_var_shader_temp, glsl_vector_type(GLSL_TYPE_UINT, comps),
         ralloc_asprintf(shader, "%s-selected", slot_name));
   }

   nir_shader_intrinsics_pass(shader, lower_to_gs_rast,
                              nir_metadata_control_flow, &rs);

   b->cursor = nir_after_impl(b->impl);
   if (gs->xfb_info) {
      unsigned n_ = mesa_vertices_per_prim(gs->info.gs.output_primitive);
      nir_def *zero = nir_imm_int(b, 0);
      nir_def *strip_length =
         rs.points ? zero : nir_load_var(b, rs.output_strip_length);
      nir_def *id_in_strip = rs.points ? zero : nir_load_var(b, rs.id_in_strip);
      nir_def *base =
         rs.points ? rs.output_id : nir_load_var(b, rs.output_strip_base);

      struct nir_xfb_info *xfb = gs->xfb_info;

      nir_def *unrolled = nir_iadd(
         b, nir_imul(b, rs.instance_id, load_geometry_param(b, grid[0])),
         rs.primitive_id);

      nir_def *n = nir_imm_int(b, n_);

      for (unsigned p_ = 0; p_ < n_; ++p_) {
         nir_def *p = nir_imm_int(b, p_);
         nir_push_if(b, poly_xfb_vertex_copy_in_strip(b, n, id_in_strip,
                                                      strip_length, p));

         /* Write XFB for each output */
         for (unsigned i = 0; i < xfb->output_count; ++i) {
            nir_xfb_output_info output = xfb->outputs[i];
            unsigned stream = xfb->buffer_to_stream[output.buffer];
            nir_push_if(b, nir_ieq_imm(b, rs.stream, stream));

            /* Get the index of this primitive in the XFB buffer. That is, the
             * base for this invocation for the stream plus the offset within
             * this invocation.
             */
            nir_def *invocation_base = poly_previous_xfb_primitives(
               b, nir_load_geometry_param_buffer_poly(b),
               nir_imm_int(b, state->static_count[stream]),
               nir_imm_int(b, state->count_index[stream]),
               nir_imm_int(b, state->info->count_words),
               nir_imm_bool(b, state->info->prefix_sum), unrolled);

            nir_def *index = poly_xfb_vertex_offset(
               b, n, invocation_base, base, id_in_strip, p,
               nir_inot(b, nir_i2b(b, nir_load_provoking_last(b))));

            nir_def *xfb_verts = load_geometry_param(b, xfb_verts[stream]);
            nir_push_if(b, nir_ult(b, index, xfb_verts));
            {
               unsigned buffer = output.buffer;
               unsigned stride = xfb->buffers[buffer].stride;

               nir_variable *var = rs.selected.outputs[output.location];
               nir_def *value =
                  var ? nir_load_var(b, var) : nir_undef(b, 4, 32);

               /* In case output.component_mask contains invalid components,
                * write out zeroes instead of blowing up validation.
                *
                * KHR-Single-GL44.enhanced_layouts.xfb_capture_inactive_output_component
                * hits this.
                */
               value = nir_pad_vector_imm_int(b, value, 0, 4);

               nir_def *addr = poly_xfb_vertex_address(
                  b, nir_load_geometry_param_buffer_poly(b), index,
                  nir_imm_int(b, buffer), nir_imm_int(b, stride),
                  nir_imm_int(b, output.offset));

               nir_store_global(
                  b, nir_channels(b, value, output.component_mask), addr);
            }
            nir_pop_if(b, NULL);
            nir_pop_if(b, NULL);
         }
         nir_pop_if(b, NULL);
      }
   }

   /* Forward each selected output to the rasterizer */
   u_foreach_bit64(slot, shader->info.outputs_written) {
      assert(rs.selected.outputs[slot] != NULL);
      nir_def *value = nir_load_var(b, rs.selected.outputs[slot]);

      /* We must only rasterize vertices from the rasterization stream. Since we
       * shade vertices across all streams, we do this by throwing away vertices
       * from non-rasterization streams (by setting a component to NaN).
       */
      if (slot == VARYING_SLOT_POS && state->info->multistream) {
         nir_def *rast_stream = nir_load_rasterization_stream(b);
         nir_def *nan = nir_imm_float(b, NAN);
         nir_def *killed = nir_vector_insert_imm(b, value, nan, 3);

         value =
            nir_bcsel(b, nir_ieq(b, rs.stream, rast_stream), value, killed);
      }

      nir_store_output(b, value, nir_imm_int(b, 0),
                       .io_semantics.location = slot);
   }

   /* The geometry shader might not write point size - ensure it does, if we're
    * rasterizing at all.
    */
   if (gs->info.gs.output_primitive == MESA_PRIM_POINTS &&
       (shader->info.outputs_written & VARYING_BIT_POS)) {

      nir_lower_default_point_size(shader);
   }

   return shader;
}

static bool
lower_gs_instr(nir_builder *b, nir_intrinsic_instr *intr, void *state_)
{
   b->cursor = nir_before_instr(&intr->instr);
   struct lower_gs_state *state = state_;

   switch (intr->intrinsic) {
   case nir_intrinsic_set_vertex_and_primitive_count: {
      if (state->info->shape != POLY_GS_SHAPE_DYNAMIC_INDEXED)
         break;

      /* All streams are merged, just pick a single instruction */
      if (nir_intrinsic_stream_id(intr) == 0) {
         poly_pad_index_gs(
            b, load_geometry_param(b, output_index_buffer),
            nir_imul_imm(b, calc_unrolled_id(b), state->info->max_indices),
            intr->src[1].ssa, nir_imm_int(b, state->info->max_indices));
      }

      break;
   }

   case nir_intrinsic_emit_primitive_poly: {
      if (state->info->shape != POLY_GS_SHAPE_DYNAMIC_INDEXED)
         break;

      poly_write_strip(
         b, load_geometry_param(b, output_index_buffer),
         nir_imul_imm(b, calc_unrolled_id(b), state->info->max_indices),
         intr->src[0].ssa,
         nir_iadd(b, calc_unrolled_index_id(b), intr->src[1].ssa),
         intr->src[2].ssa,
         nir_imm_ivec3(b, nir_intrinsic_stream_id(intr),
                       stream_multiplier(b->shader),
                       nir_verts_in_output_prim(b->shader)));
      break;
   }

   case nir_intrinsic_store_output:
   case nir_intrinsic_select_vertex_poly:
      break;

   default:
      return false;
   }

   nir_instr_remove(&intr->instr);
   return true;
}

static bool
collect_components(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   uint8_t *counts = data;
   if (intr->intrinsic != nir_intrinsic_store_output)
      return false;

   unsigned count = nir_intrinsic_component(intr) +
                    util_last_bit(nir_intrinsic_write_mask(intr));

   unsigned loc =
      nir_intrinsic_io_semantics(intr).location + nir_src_as_uint(intr->src[1]);

   uint8_t *total_count = &counts[loc];

   *total_count = MAX2(*total_count, count);
   return true;
}

struct poly_xfb_key {
   uint8_t streams;
   uint8_t buffers_written;
   uint8_t buffer_to_stream[NIR_MAX_XFB_BUFFERS];
   int8_t count_index[4];
   uint16_t stride[NIR_MAX_XFB_BUFFERS];
   uint16_t output_end[NIR_MAX_XFB_BUFFERS];
   int16_t static_count[POLY_MAX_VERTEX_STREAMS];
   uint16_t invocations;
   uint16_t vertices_per_prim;
};

/*
 * Create the pre-GS shader. This is a small compute 1x1x1 kernel that produces
 * an indirect draw to rasterize the produced geometry, as well as updates
 * transform feedback offsets and counters as applicable.
 */
static nir_shader *
create_pre_gs(struct poly_xfb_key *key,
              const nir_shader_compiler_options *options)
{
   nir_builder b_ = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, options,
                                                   "Pre-GS patch up");
   nir_builder *b = &b_;

   poly_pre_gs(
      b, nir_load_geometry_param_buffer_poly(b), nir_imm_int(b, key->streams),
      nir_imm_int(b, key->buffers_written),
      nir_imm_ivec4(b, key->buffer_to_stream[0], key->buffer_to_stream[1],
                    key->buffer_to_stream[2], key->buffer_to_stream[3]),
      nir_imm_ivec4(b, key->count_index[0], key->count_index[1],
                    key->count_index[2], key->count_index[3]),
      nir_imm_ivec4(b, key->stride[0], key->stride[1], key->stride[2],
                    key->stride[3]),
      nir_imm_ivec4(b, key->output_end[0], key->output_end[1],
                    key->output_end[2], key->output_end[3]),
      nir_imm_ivec4(b, key->static_count[0], key->static_count[1],
                    key->static_count[2], key->static_count[3]),
      nir_imm_int(b, key->invocations), nir_imm_int(b, key->vertices_per_prim),
      nir_load_stat_query_address_poly(b,
                                       .base = PIPE_STAT_QUERY_GS_INVOCATIONS),
      nir_load_stat_query_address_poly(b,
                                       .base = PIPE_STAT_QUERY_GS_PRIMITIVES),
      nir_load_stat_query_address_poly(b, .base = PIPE_STAT_QUERY_C_PRIMITIVES),
      nir_load_stat_query_address_poly(b,
                                       .base = PIPE_STAT_QUERY_C_INVOCATIONS));
   return b->shader;
}

static bool
rewrite_invocation_id(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_invocation_id)
      return false;

   b->cursor = nir_before_instr(&intr->instr);
   nir_def_replace(&intr->def, nir_u2uN(b, data, intr->def.bit_size));
   return true;
}

/*
 * Geometry shader instancing allows a GS to run multiple times. The number of
 * times is statically known and small. It's easiest to turn this into a loop
 * inside the GS, to avoid the feature "leaking" outside and affecting e.g. the
 * counts.
 */
static void
lower_gs_instancing(nir_shader *gs)
{
   unsigned nr_invocations = gs->info.gs.invocations;
   nir_function_impl *impl = nir_shader_get_entrypoint(gs);

   /* Each invocation can produce up to the shader-declared max_vertices, so
    * multiply it up for proper bounds check. Emitting more than the declared
    * max_vertices per invocation results in undefined behaviour, so erroneously
    * emitting more as asked on early invocations is a perfectly cromulent
    * behvaiour.
    */
   gs->info.gs.vertices_out *= gs->info.gs.invocations;

   /* Get the original function */
   nir_cf_list list;
   nir_cf_extract(&list, nir_before_impl(impl), nir_after_impl(impl));

   /* Create a builder for the wrapped function */
   nir_builder b = nir_builder_at(nir_after_block(nir_start_block(impl)));

   nir_variable *i =
      nir_local_variable_create(impl, glsl_uintN_t_type(16), NULL);
   nir_store_var(&b, i, nir_imm_intN_t(&b, 0, 16), ~0);
   nir_def *index = NULL;

   /* Create a loop in the wrapped function */
   nir_loop *loop = nir_push_loop(&b);
   {
      index = nir_load_var(&b, i);
      nir_break_if(&b, nir_uge_imm(&b, index, nr_invocations));

      b.cursor = nir_cf_reinsert(&list, b.cursor);
      nir_store_var(&b, i, nir_iadd_imm(&b, index, 1), ~0);

      /* Make sure we end the primitive between invocations. If the geometry
       * shader already ended the primitive, this will get optimized out.
       */
      nir_end_primitive(&b);
   }
   nir_pop_loop(&b, loop);

   /* We've mucked about with control flow */
   nir_progress(true, impl, nir_metadata_none);

   /* Use the loop counter as the invocation ID each iteration */
   nir_shader_intrinsics_pass(gs, rewrite_invocation_id,
                              nir_metadata_control_flow, index);
}

static unsigned
calculate_max_indices(enum mesa_prim prim, unsigned verts)
{
   /* Points do not need primitive count added. Other topologies do. If we have
    * a static primitive count, we use that. Otherwise, we use a worst case
    * estimate that primitives are emitted one-by-one.
    */
   if (prim == MESA_PRIM_POINTS)
      return verts;
   else
      return verts + (verts / mesa_vertices_per_prim(prim));
}

struct topology_ctx {
   struct poly_gs_info *info;
   uint32_t topology[384];
};

static bool
evaluate_topology(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   struct topology_ctx *ctx = data;
   struct poly_gs_info *info = ctx->info;
   if (intr->intrinsic != nir_intrinsic_emit_primitive_poly)
      return false;

   /* All emit-primitives must execute exactly once. That happens if everything
    * is in the start block. Strictly we could relax this (to handle
    * if-statements interleaved with other stuff).
    */
   if (intr->instr.block != nir_start_block(b->impl)) {
      info->shape = POLY_GS_SHAPE_DYNAMIC_INDEXED;
      return false;
   }

   /* The topology must be static */
   if (!nir_src_is_const(intr->src[0]) || !nir_src_is_const(intr->src[1]) ||
       !nir_src_is_const(intr->src[2])) {

      info->shape = POLY_GS_SHAPE_DYNAMIC_INDEXED;
      return false;
   }

   _poly_write_strip(
      ctx->topology, nir_src_as_uint(intr->src[0]),
      nir_src_as_uint(intr->src[1]), nir_src_as_uint(intr->src[2]),
      nir_intrinsic_stream_id(intr), stream_multiplier(b->shader),
      nir_verts_in_output_prim(b->shader));
   return false;
}

/*
 * Pattern match the index buffer with restart against a list topology:
 *
 *    0, 1, 2, -1, 3, 4, 5, ...
 */
static bool
match_list_topology(struct poly_gs_info *info, uint32_t count,
                    uint32_t *topology, bool has_restart)
{
   unsigned count_with_restart = count + has_restart;

   /* Must be an integer number of primitives. Last restart is dropped. */
   if ((info->max_indices + has_restart) % count_with_restart)
      return false;

   /* Must match the list topology */
   for (unsigned i = 0; i < info->max_indices; ++i) {
      bool restart = (i % count_with_restart) == count;
      uint32_t expected = restart ? -1 : (i - (i / count_with_restart));

      if (topology[i] != expected)
         return false;
   }

   /* If we match, rewrite the topology and drop indexing */
   info->shape = POLY_GS_SHAPE_STATIC_PER_INSTANCE;
   info->mode = u_decomposed_prim(info->mode);
   info->max_indices =
      ((info->max_indices + has_restart) / count_with_restart) * count;
   return true;
}

static bool
is_strip_topology(uint32_t *indices, uint32_t index_count)
{
   for (unsigned i = 0; i < index_count; ++i) {
      if (indices[i] != i)
         return false;
   }

   return true;
}

/*
 * To handle the general case of geometry shaders generating dynamic topologies,
 * we translate geometry shaders into compute shaders that write an index
 * buffer. In practice, many geometry shaders have static topologies that can be
 * determined at compile-time. By identifying these, we can avoid the dynamic
 * index buffer allocation and writes. optimize_static_topology tries to
 * statically determine the topology, then translating it to one of:
 *
 * 1. Non-indexed line/triangle lists without instancing.
 * 2. Non-indexed line/triangle strips, instanced per input primitive.
 * 3. Static index buffer, instanced per input primitive.
 *
 * If the geometry shader has no side effect, the only job of the compute shader
 * is writing this index buffer, so this optimization effectively eliminates the
 * compute dispatch entirely. That means simple VS+GS pipelines turn into simple
 * VS(compute) + GS(vertex) sequences without auxiliary programs.
 */
static void
optimize_static_topology(struct poly_gs_info *info, nir_shader *gs)
{
   struct topology_ctx ctx = {.info = info};
   bool has_restart = info->mode != MESA_PRIM_POINTS;
   nir_shader_intrinsics_pass(gs, evaluate_topology, nir_metadata_all, &ctx);
   if (info->shape == POLY_GS_SHAPE_DYNAMIC_INDEXED)
      return;

   /* We can always drop the trailing restart index */
   if (has_restart && info->max_indices)
      info->max_indices--;

   /* Try to pattern match a list topology */
   unsigned count = nir_verts_in_output_prim(gs);
   if (match_list_topology(info, count, ctx.topology, has_restart))
      return;

   /* Try to pattern match a strip topology */
   if (is_strip_topology(ctx.topology, info->max_indices)) {
      info->shape = POLY_GS_SHAPE_STATIC_PER_PRIM;
      return;
   }

   /* Otherwise, use a small static index buffer. There's no theoretical reason
    * to bound this, but we want small serialized shader info structs. We assume
    * that large static index buffers are rare and hence fall back to dynamic.
    *
    * XXX: check if this holds with streams.
    */
   if (info->max_indices >= ARRAY_SIZE(info->topology)) {
      info->shape = POLY_GS_SHAPE_DYNAMIC_INDEXED;
      return;
   }

   for (unsigned i = 0; i < info->max_indices; ++i) {
      assert((ctx.topology[i] < 0xFF || ctx.topology[i] == ~0) && "small");
      info->topology[i] = ctx.topology[i];
   }

   info->shape = POLY_GS_SHAPE_STATIC_INDEXED;
}

bool
poly_nir_lower_gs(nir_shader *gs, nir_shader **gs_count, nir_shader **gs_copy,
                  nir_shader **pre_gs, struct poly_gs_info *info)
{
   /* Lower I/O as assumed by the rest of GS lowering */
   if (gs->xfb_info != NULL) {
      /* Fold constant offset srcs for IO. */
      NIR_PASS(_, gs, nir_opt_constant_folding);
      NIR_PASS(_, gs, nir_io_add_intrinsic_xfb_info);
   }

   NIR_PASS(_, gs, nir_lower_io_to_scalar, nir_var_shader_out, NULL, NULL);

   /* Collect output component counts so we can size the geometry output buffer
    * appropriately, instead of assuming everything is vec4.
    */
   uint8_t component_counts[NUM_TOTAL_VARYING_SLOTS] = {0};
   nir_shader_intrinsics_pass(gs, collect_components, nir_metadata_all,
                              component_counts);

   /* If geometry shader instancing is used, lower it away before linking
    * anything. Otherwise, smash the invocation ID to zero.
    */
   if (gs->info.gs.invocations != 1) {
      lower_gs_instancing(gs);
   } else {
      nir_function_impl *impl = nir_shader_get_entrypoint(gs);
      nir_builder b = nir_builder_at(nir_before_impl(impl));

      nir_shader_intrinsics_pass(gs, rewrite_invocation_id,
                                 nir_metadata_control_flow, nir_imm_int(&b, 0));
   }

   NIR_PASS(_, gs, nir_shader_intrinsics_pass, lower_gs_inputs,
            nir_metadata_control_flow, NULL);

   /* Lower geometry shader writes to contain all of the required counts, so we
    * know where in the various buffers we should write vertices.
    */
   NIR_PASS(_, gs, lower_gs_intrinsics);

   /* Clean up after all that lowering we did */
   bool progress = false;
   do {
      progress = false;
      NIR_PASS(progress, gs, nir_lower_var_copies);
      NIR_PASS(progress, gs, nir_lower_variable_initializers,
               nir_var_shader_temp);
      NIR_PASS(progress, gs, nir_lower_vars_to_ssa);
      NIR_PASS(progress, gs, nir_opt_copy_prop);
      NIR_PASS(progress, gs, nir_opt_constant_folding);
      NIR_PASS(progress, gs, nir_opt_algebraic);
      NIR_PASS(progress, gs, nir_opt_cse);
      NIR_PASS(progress, gs, nir_opt_dead_cf);
      NIR_PASS(progress, gs, nir_opt_dce);

      /* Unrolling lets us statically determine counts more often, which
       * otherwise would not be possible with multiple invocations even in the
       * simplest of cases.
       */
      NIR_PASS(progress, gs, nir_opt_loop_unroll);
   } while (progress);

   /* If we know counts at compile-time we can simplify, so try to figure out
    * the counts statically.
    */
   struct lower_gs_state gs_state = {.info = info};

   *info = (struct poly_gs_info){
      .mode = gs->info.gs.output_primitive,
      .xfb = gs->xfb_info != NULL,
      .shape = -1,
      .multistream = gs->info.gs.active_stream_mask & ~1,
   };

   int static_indices[4] = {0};
   nir_gs_count_vertices_and_primitives(gs, NULL, static_indices,
                                        gs_state.static_count, 4);

   STATIC_ASSERT(ARRAY_SIZE(gs_state.count_index) ==
                 ARRAY_SIZE(gs_state.static_count));

   /* Anything we don't know statically will be tracked by the count buffer.
    * Determine the layout for it.
    */
   for (unsigned i = 0; i < ARRAY_SIZE(gs_state.count_index); ++i) {
      gs_state.count_index[i] =
         (gs_state.static_count[i] < 0) ? info->count_words++ : -1;
   }

   /* Using the gathered static counts, choose the index buffer stride. */
   info->max_indices = static_indices[0];
   if (static_indices[0] < 0) {
      info->max_indices = calculate_max_indices(gs->info.gs.output_primitive,
                                                gs->info.gs.vertices_out);
   }

   info->prefix_sum = info->count_words > 0 && gs->xfb_info != NULL;

   if (static_indices[0] >= 0) {
      optimize_static_topology(info, gs);
   } else {
      info->shape = POLY_GS_SHAPE_DYNAMIC_INDEXED;
   }

   *gs_copy = create_gs_rast_shader(gs, &gs_state);

   NIR_PASS(_, gs, nir_shader_intrinsics_pass, lower_id,
            nir_metadata_control_flow, NULL);

   NIR_PASS(_, gs, nir_lower_idiv,
            &(const nir_lower_idiv_options){.allow_fp16 = true});

   /* All those variables we created should've gone away by now */
   NIR_PASS(_, gs, nir_remove_dead_variables, nir_var_function_temp, NULL);

   /* If there is any unknown count, we need a geometry count shader */
   if (info->count_words > 0)
      *gs_count = create_geometry_count_shader(gs, &gs_state);
   else
      *gs_count = NULL;

   /* Strip stores and atomics */
   do {
      progress = false;
      NIR_PASS(progress, gs, nir_shader_intrinsics_pass,
               strip_side_effect_from_main, nir_metadata_control_flow,
               (void *)true);

      NIR_PASS(progress, gs, nir_opt_dce);
      NIR_PASS(progress, gs, nir_opt_dead_cf);
   } while (progress);

   NIR_PASS(_, gs, nir_shader_intrinsics_pass, lower_gs_instr,
            nir_metadata_none, &gs_state);

   /* Clean up after all that lowering we did */
   nir_lower_global_vars_to_local(gs);
   do {
      progress = false;
      NIR_PASS(progress, gs, nir_lower_var_copies);
      NIR_PASS(progress, gs, nir_lower_variable_initializers,
               nir_var_shader_temp);
      NIR_PASS(progress, gs, nir_lower_vars_to_ssa);
      NIR_PASS(progress, gs, nir_opt_copy_prop);
      NIR_PASS(progress, gs, nir_opt_constant_folding);
      NIR_PASS(progress, gs, nir_opt_algebraic);
      NIR_PASS(progress, gs, nir_opt_cse);
      NIR_PASS(progress, gs, nir_opt_dead_cf);
      NIR_PASS(progress, gs, nir_opt_dce);
      NIR_PASS(progress, gs, nir_opt_loop_unroll);

   } while (progress);

   /* Strip remaining atomics, but not stores - since those are from us */
   do {
      progress = false;
      NIR_PASS(progress, gs, nir_shader_intrinsics_pass,
               strip_side_effect_from_main, nir_metadata_control_flow,
               (void *)false);

      NIR_PASS(progress, gs, nir_opt_dce);
      NIR_PASS(progress, gs, nir_opt_dead_cf);
   } while (progress);

   /* All those variables we created should've gone away by now */
   NIR_PASS(_, gs, nir_remove_dead_variables, nir_var_function_temp, NULL);

   NIR_PASS(_, gs, nir_shader_intrinsics_pass, lower_id,
            nir_metadata_control_flow, NULL);

   /* Gather information required for transform feedback / query programs */
   struct nir_xfb_info *xfb = gs->xfb_info;

   struct poly_xfb_key key = {
      .streams = gs->info.gs.active_stream_mask,
      .invocations = gs->info.gs.invocations,
      .vertices_per_prim = nir_verts_in_output_prim(gs),
   };

   STATIC_ASSERT(ARRAY_SIZE(key.buffer_to_stream) == ARRAY_SIZE(key.stride));

   for (unsigned i = 0; i < ARRAY_SIZE(gs_state.count_index); ++i) {
      key.count_index[i] = gs_state.count_index[i];
      key.static_count[i] = gs_state.static_count[i];
   }

   if (xfb) {
      key.buffers_written = xfb->buffers_written;
      for (unsigned i = 0; i < ARRAY_SIZE(key.buffer_to_stream); ++i) {
         key.buffer_to_stream[i] = xfb->buffer_to_stream[i];
         key.stride[i] = xfb->buffers[i].stride;
      }

      /* Any buffer that is written is treated as writing at least 1 byte. If
       * nothing is actually written, this ensures correctness with XFB queries.
       * See dEQP-VK.transform_feedback.simple.multiquery_omit_write_3.
       */
      u_foreach_bit(b, xfb->buffers_written) {
         key.output_end[b] = 1;
      }

      for (unsigned i = 0; i < xfb->output_count; ++i) {
         nir_xfb_output_info output = xfb->outputs[i];
         unsigned buffer = xfb->outputs[i].buffer;

         unsigned words_written = util_bitcount(output.component_mask);
         unsigned bytes_written = words_written * 4;
         unsigned output_end = output.offset + bytes_written;
         key.output_end[buffer] = MAX2(key.output_end[buffer], output_end);
      }
   }

   /* Create auxiliary programs */
   *pre_gs = create_pre_gs(&key, gs->options);
   return true;
}

/*
 * Vertex shaders (tessellation evaluation shaders) before a geometry shader run
 * as a dedicated compute prepass. They are invoked as (count, instances, 1).
 * Their linear ID is therefore (instances * num vertices) + vertex ID.
 *
 * This function lowers their vertex shader I/O to compute.
 *
 * Vertex ID becomes an index buffer pull (without applying the topology). Store
 * output becomes a store into the global vertex output buffer.
 */
static bool
lower_vs_before_gs(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_store_output)
      return false;

   b->cursor = nir_instr_remove(&intr->instr);
   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   nir_def *location = nir_iadd_imm(b, intr->src[1].ssa, sem.location);

   nir_def *vp = nir_load_vertex_param_buffer_poly(b);

   /* Instancing is unrolled during tessellation so nr_verts is ignored. */
   nir_def *nr_verts = b->shader->info.stage == MESA_SHADER_VERTEX ?
                       poly_input_vertices(b, vp) : nir_imm_int(b, 0);

   nir_def *instance_id, *primitive_id;
   if (b->shader->info.stage == MESA_SHADER_VERTEX &&
       !b->shader->info.vs.tes_poly) {
      primitive_id = nir_load_vertex_id_zero_base(b);
      instance_id = nir_load_instance_id(b);
   } else {
      primitive_id = load_primitive_id(b);
      instance_id = load_instance_id(b);
   }

   nir_def *linear_id =
      nir_iadd(b, nir_imul(b, instance_id, nr_verts), primitive_id);

   nir_def *addr = poly_vertex_output_address(
      b, vp, nir_imm_int64(b, b->shader->info.outputs_written), linear_id,
      location);

   assert(nir_src_bit_size(intr->src[0]) == 32);
   addr = nir_iadd_imm(b, addr, nir_intrinsic_component(intr) * 4);

   nir_store_global(b, intr->src[0].ssa, addr,
                    .write_mask = nir_intrinsic_write_mask(intr));
   return true;
}

bool
poly_nir_lower_vs_before_gs(struct nir_shader *vs)
{
   /* Lower vertex stores to memory stores */
   return nir_shader_intrinsics_pass(vs, lower_vs_before_gs,
                                     nir_metadata_control_flow, NULL);
}
