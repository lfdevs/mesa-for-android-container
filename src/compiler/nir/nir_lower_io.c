/*
 * Copyright © 2014 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/*
 * This lowering pass converts references to input/output variables with
 * loads/stores to actual input/output intrinsics.
 */

#include "nir.h"
#include "nir_builder.h"
#include "nir_deref.h"

#include "util/u_math.h"

struct lower_io_state {
   void *dead_ctx;
   nir_builder builder;
   int (*type_size)(const struct glsl_type *type, bool);
   nir_variable_mode modes;
   nir_lower_io_options options;
   struct set variable_names;
};

static const char *
add_variable_name(struct lower_io_state *state, const char *name)
{
   if (!name)
      return NULL;

   bool found = false;
   struct set_entry *entry = _mesa_set_search_or_add(&state->variable_names, name, &found);
   if (!found)
      entry->key = (void *)ralloc_strdup(state->builder.shader, name);
   return entry->key;
}


/**
 * Some inputs and outputs are arrayed, meaning that there is an extra level
 * of array indexing to handle mismatches between the shader interface and the
 * dispatch pattern of the shader.  For instance, geometry shaders are
 * executed per-primitive while their inputs and outputs are specified
 * per-vertex so all inputs and outputs have to be additionally indexed with
 * the vertex index within the primitive.
 */
bool
nir_is_arrayed_io(const nir_variable *var, mesa_shader_stage stage)
{
   if (var->data.patch || !glsl_type_is_array(var->type))
      return false;

   if (var->data.per_view) {
      /* Nested arrayed outputs (both per-view and per-{vertex,primitive}) are
       * unsupported. */
      assert(stage == MESA_SHADER_VERTEX);
      assert(var->data.mode == nir_var_shader_out);
      return true;
   }

   if (stage == MESA_SHADER_MESH) {
      /* NV_mesh_shader: this is flat array for the whole workgroup. */
      if (var->data.location == VARYING_SLOT_PRIMITIVE_INDICES)
         return var->data.per_primitive;
   }

   if (var->data.mode == nir_var_shader_in) {
      if (var->data.per_vertex) {
         assert(stage == MESA_SHADER_FRAGMENT);
         return true;
      }

      return stage == MESA_SHADER_GEOMETRY ||
             stage == MESA_SHADER_TESS_CTRL ||
             stage == MESA_SHADER_TESS_EVAL;
   }

   if (var->data.mode == nir_var_shader_out)
      return stage == MESA_SHADER_TESS_CTRL ||
             stage == MESA_SHADER_MESH;

   return false;
}

/* Add `offset_diff_bytes` bytes to the offset used by `intr`. Takes the
 * offset_shift used by `intr` (if any) into account and, if needed, adjusts
 * it in order to be able to represent the resulting offset in full precision.
 */
nir_io_offset
nir_io_offset_iadd(nir_builder *b, nir_intrinsic_instr *intr,
                   int offset_diff_bytes)
{
   unsigned offset_diff;
   unsigned base_shift;
   unsigned offset_shift;

   if (nir_intrinsic_has_offset_shift(intr)) {
      unsigned cur_offset_shift = nir_intrinsic_offset_shift(intr);

      if (util_is_aligned(offset_diff_bytes, (uintmax_t)1 << cur_offset_shift)) {
         /* If the byte offset is properly aligned, we can just shift it and
          * keep the current offset_shift.
          */
         offset_diff = offset_diff_bytes >> cur_offset_shift;
         base_shift = 0;
         offset_shift = cur_offset_shift;
      } else {
         /* TODO add support for adjusting the base index. */
         assert(!nir_intrinsic_has_base(intr) || nir_intrinsic_base(intr) == 0);

         /* Otherwise, we have to lower offset_shift in order to not lose
          * precision. We also have to shift the original base offset left to
          * make sure it uses the same units.
          */
         offset_shift = ffs(offset_diff_bytes) - 1;
         offset_diff = offset_diff_bytes >> offset_shift;
         base_shift = cur_offset_shift - offset_shift;
      }
   } else {
      offset_diff = offset_diff_bytes;
      base_shift = 0;
      offset_shift = 0;
   }

   nir_src *base_offset_src = nir_get_io_offset_src(intr);
   assert(base_offset_src);

   nir_def *base_offset = base_offset_src->ssa;
   nir_def *offset =
      nir_iadd_imm(b, nir_ishl_imm(b, base_offset, base_shift), offset_diff);

   return (nir_io_offset){
      .def = offset,
      .shift = offset_shift,
   };
}

/* Set the offset src and offset_shift of `intr` to `offset`. */
void
nir_set_io_offset(nir_intrinsic_instr *intr, nir_io_offset offset)
{
   nir_src *offset_src = nir_get_io_offset_src(intr);
   assert(offset_src);

   if (offset_src->ssa) {
      nir_src_rewrite(offset_src, offset.def);
   } else {
      *offset_src = nir_src_for_ssa(offset.def);
   }

   if (nir_intrinsic_has_offset_shift(intr)) {
      /* TODO add support for adjusting the base index. */
      assert(!nir_intrinsic_has_base(intr) || nir_intrinsic_base(intr) == 0);

      nir_intrinsic_set_offset_shift(intr, offset.shift);
   } else {
      assert(offset.shift == 0);
   }
}

void
nir_add_io_offset(nir_builder *b, nir_intrinsic_instr *intr,
                  int offset_diff_bytes)
{
   nir_set_io_offset(intr, nir_io_offset_iadd(b, intr, offset_diff_bytes));
}

static bool
uses_high_dvec2_semantic(struct lower_io_state *state,
                         const nir_variable *var)
{
   return state->builder.shader->info.stage == MESA_SHADER_VERTEX &&
          state->options & nir_lower_io_lower_64bit_to_32_new &&
          var->data.mode == nir_var_shader_in &&
          glsl_type_is_dual_slot(glsl_without_array(var->type));
}

static unsigned
get_number_of_slots(struct lower_io_state *state,
                    const nir_variable *var)
{
   const struct glsl_type *type = var->type;

   if (nir_is_arrayed_io(var, state->builder.shader->info.stage)) {
      assert(glsl_type_is_array(type));
      type = glsl_get_array_element(type);
   }

   /* NV_mesh_shader:
    * PRIMITIVE_INDICES is a flat array, not a proper arrayed output,
    * as opposed to D3D-style mesh shaders where it's addressed by
    * the primitive index.
    * Prevent assigning several slots to primitive indices,
    * to avoid some issues.
    */
   if (state->builder.shader->info.stage == MESA_SHADER_MESH &&
       var->data.location == VARYING_SLOT_PRIMITIVE_INDICES &&
       !nir_is_arrayed_io(var, state->builder.shader->info.stage))
      return 1;

   return state->type_size(type, var->data.bindless) /
          (uses_high_dvec2_semantic(state, var) ? 2 : 1);
}

static nir_def *
get_io_offset(nir_builder *b, nir_deref_instr *deref,
              nir_def **array_index,
              int (*type_size)(const struct glsl_type *, bool),
              unsigned *component, bool bts)
{
   nir_deref_path path;
   nir_deref_path_init(&path, deref, NULL);

   assert(path.path[0]->deref_type == nir_deref_type_var);
   nir_deref_instr **p = &path.path[1];

   /* For arrayed I/O (e.g., per-vertex input arrays in geometry shader
    * inputs), skip the outermost array index.  Process the rest normally.
    */
   if (array_index != NULL) {
      assert((*p)->deref_type == nir_deref_type_array);
      *array_index = (*p)->arr.index.ssa;
      p++;
   }

   if (path.path[0]->var->data.compact && nir_src_is_const((*p)->arr.index)) {
      assert((*p)->deref_type == nir_deref_type_array);
      assert(glsl_type_is_scalar((*p)->type));

      /* We always lower indirect dereferences for "compact" array vars. */
      const unsigned index = nir_src_as_uint((*p)->arr.index);
      const unsigned total_offset = *component + index;
      const unsigned slot_offset = total_offset / 4;
      *component = total_offset % 4;
      return nir_imm_int(b, type_size(glsl_vec4_type(), bts) * slot_offset);
   }

   /* Just emit code and let constant-folding go to town */
   nir_def *offset = nir_imm_int(b, 0);

   for (; *p; p++) {
      if ((*p)->deref_type == nir_deref_type_array) {
         unsigned size = type_size((*p)->type, bts);

         nir_def *mul =
            nir_amul_imm(b, (*p)->arr.index.ssa, size);

         offset = nir_iadd(b, offset, mul);
      } else if ((*p)->deref_type == nir_deref_type_struct) {
         /* p starts at path[1], so this is safe */
         nir_deref_instr *parent = *(p - 1);

         unsigned field_offset = 0;
         for (unsigned i = 0; i < (*p)->strct.index; i++) {
            field_offset += type_size(glsl_get_struct_field(parent->type, i), bts);
         }
         offset = nir_iadd_imm(b, offset, field_offset);
      } else {
         UNREACHABLE("Unsupported deref type");
      }
   }

   nir_deref_path_finish(&path);

   return offset;
}

static bool
is_medium_precision(const nir_shader *shader, const nir_variable *var)
{
   if (shader->options->io_options & nir_io_mediump_is_32bit)
      return false;

   return var->data.precision == GLSL_PRECISION_MEDIUM ||
          var->data.precision == GLSL_PRECISION_LOW;
}

static enum glsl_interp_mode
get_interp_mode(const nir_variable *var)
{
   unsigned interp_mode = var->data.interpolation;

   /* INTERP_MODE_NONE is an artifact of OpenGL. Change it to SMOOTH
    * to enable CSE between load_barycentric_pixel(NONE->SMOOTH) and
    * load_barycentric_pixel(SMOOTH), which also enables IO vectorization when
    * one component originally had NONE and an adjacent component had SMOOTH.
    *
    * Color varyings must preserve NONE. NONE for colors means that
    * glShadeModel determines the interpolation mode.
    */
   if (var->data.location != VARYING_SLOT_COL0 &&
       var->data.location != VARYING_SLOT_COL1 &&
       var->data.location != VARYING_SLOT_BFC0 &&
       var->data.location != VARYING_SLOT_BFC1 &&
       interp_mode == INTERP_MODE_NONE)
      return INTERP_MODE_SMOOTH;

   return interp_mode;
}

static nir_def *
simplify_offset_src(nir_builder *b, nir_def *offset, unsigned num_slots)
{
   /* Force index=0 for any indirect access to array[1]. */
   if (num_slots == 1 && !nir_def_is_const(offset))
      return nir_imm_int(b, 0);

   return offset;
}

static nir_def *
emit_load(struct lower_io_state *state,
          nir_def *array_index, nir_variable *var, nir_def *offset,
          unsigned component, unsigned num_components, unsigned bit_size,
          nir_alu_type dest_type, bool high_dvec2)
{
   nir_builder *b = &state->builder;
   const nir_shader *nir = b->shader;
   nir_variable_mode mode = var->data.mode;
   nir_def *barycentric = NULL;

   nir_intrinsic_op op;
   switch (mode) {
   case nir_var_shader_in:
      if (nir->info.stage == MESA_SHADER_FRAGMENT &&
          state->options & nir_lower_io_use_interpolated_input_intrinsics &&
          var->data.interpolation != INTERP_MODE_FLAT &&
          !var->data.per_primitive) {
         if (var->data.interpolation == INTERP_MODE_EXPLICIT ||
             var->data.per_vertex) {
            assert(array_index != NULL);
            op = nir_intrinsic_load_input_vertex;
         } else {
            assert(array_index == NULL);

            nir_intrinsic_op bary_op;
            if (var->data.sample)
               bary_op = nir_intrinsic_load_barycentric_sample;
            else if (var->data.centroid)
               bary_op = nir_intrinsic_load_barycentric_centroid;
            else
               bary_op = nir_intrinsic_load_barycentric_pixel;

            barycentric = nir_load_barycentric(&state->builder, bary_op,
                                               get_interp_mode(var));
            op = nir_intrinsic_load_interpolated_input;
         }
      } else {
         if (var->data.per_primitive)
            op = nir_intrinsic_load_per_primitive_input;
         else if (array_index)
            op = nir_intrinsic_load_per_vertex_input;
         else
            op = nir_intrinsic_load_input;
      }
      break;
   case nir_var_shader_out:
      if (!array_index)
         op = nir_intrinsic_load_output;
      else if (var->data.per_primitive)
         op = nir_intrinsic_load_per_primitive_output;
      else if (var->data.per_view)
         op = nir_intrinsic_load_per_view_output;
      else
         op = nir_intrinsic_load_per_vertex_output;
      break;
   case nir_var_uniform:
      op = nir_intrinsic_load_uniform;
      break;
   case nir_var_mem_pixel_local_in:
   case nir_var_mem_pixel_local_inout:
      assert(!array_index);
      op = nir_intrinsic_load_pixel_local;
      break;
   default:
      UNREACHABLE("Unknown variable mode");
   }

   nir_intrinsic_instr *load =
      nir_intrinsic_instr_create(state->builder.shader, op);
   load->num_components = num_components;
   load->name = add_variable_name(state, var->name);

   nir_intrinsic_set_base(load, var->data.driver_location);
   if (nir_intrinsic_has_range(load)) {
      const struct glsl_type *type = var->type;
      if (array_index)
         type = glsl_get_array_element(type);

      unsigned var_size = state->type_size(type, var->data.bindless);
      if (var_size)
         nir_intrinsic_set_range(load, var_size);
      else
         nir_intrinsic_set_range(load, ~0);
   }

   if (mode == nir_var_shader_in || mode == nir_var_shader_out)
      nir_intrinsic_set_component(load, component);

   if (nir_intrinsic_has_access(load))
      nir_intrinsic_set_access(load, var->data.access);

   nir_intrinsic_set_dest_type(load, dest_type);

   if (op == nir_intrinsic_load_pixel_local) {
      assert(var && var->data.image.format != PIPE_FORMAT_NONE);
      nir_intrinsic_set_format(load, var->data.image.format);
   }

   if (load->intrinsic != nir_intrinsic_load_uniform) {
      int location = var->data.location;
      unsigned num_slots = get_number_of_slots(state, var);

      /* Maximum values in nir_io_semantics. */
      assert(num_slots <= 63);
      assert(location >= 0 && location + num_slots <= NUM_TOTAL_VARYING_SLOTS);

      nir_io_semantics semantics = { 0 };
      semantics.location = location;
      semantics.num_slots = num_slots;
      semantics.fb_fetch_output = var->data.fb_fetch_output;
      if (semantics.fb_fetch_output) {
         semantics.fb_fetch_output_coherent =
            !!(var->data.access & ACCESS_COHERENT);
      }
      semantics.medium_precision = is_medium_precision(b->shader, var);
      semantics.high_dvec2 = high_dvec2;
      /* "per_vertex" is misnamed. It means "explicit interpolation with
       * the original vertex order", which is a stricter version of
       * INTERP_MODE_EXPLICIT.
       */
      semantics.interp_explicit_strict = var->data.per_vertex;
      nir_intrinsic_set_io_semantics(load, semantics);

      offset = simplify_offset_src(b, offset, num_slots);
   }

   if (array_index) {
      load->src[0] = nir_src_for_ssa(array_index);
      load->src[1] = nir_src_for_ssa(offset);
   } else if (barycentric) {
      load->src[0] = nir_src_for_ssa(barycentric);
      load->src[1] = nir_src_for_ssa(offset);
   } else {
      load->src[0] = nir_src_for_ssa(offset);
   }

   nir_def_init(&load->instr, &load->def, num_components, bit_size);
   nir_builder_instr_insert(b, &load->instr);

   return &load->def;
}

static nir_def *
lower_load(nir_intrinsic_instr *intrin, struct lower_io_state *state,
           nir_def *array_index, nir_variable *var, nir_def *offset,
           unsigned component, const struct glsl_type *type)
{
   const bool lower_double = !glsl_type_is_integer(type) && state->options & nir_lower_io_lower_64bit_float_to_32;
   if (intrin->def.bit_size == 64 &&
       (lower_double || (state->options & (nir_lower_io_lower_64bit_to_32_new |
                                           nir_lower_io_lower_64bit_to_32)))) {
      nir_builder *b = &state->builder;
      bool use_high_dvec2_semantic = uses_high_dvec2_semantic(state, var);

      /* Each slot is a dual slot, so divide the offset within the variable
       * by 2.
       */
      if (use_high_dvec2_semantic)
         offset = nir_ushr_imm(b, offset, 1);

      const unsigned slot_size = state->type_size(glsl_dvec_type(2), false);

      nir_def *comp64[4];
      assert(component == 0 || component == 2);
      unsigned dest_comp = 0;
      bool high_dvec2 = false;
      while (dest_comp < intrin->def.num_components) {
         const unsigned num_comps =
            MIN2(intrin->def.num_components - dest_comp,
                 (4 - component) / 2);

         nir_def *data32 =
            emit_load(state, array_index, var, offset, component,
                      num_comps * 2, 32, nir_type_uint32, high_dvec2);
         for (unsigned i = 0; i < num_comps; i++) {
            comp64[dest_comp + i] =
               nir_pack_64_2x32(b, nir_channels(b, data32, 3 << (i * 2)));
         }

         /* Only the first store has a component offset */
         component = 0;
         dest_comp += num_comps;

         if (use_high_dvec2_semantic) {
            /* Increment the offset when we wrap around the dual slot. */
            if (high_dvec2)
               offset = nir_iadd_imm(b, offset, slot_size);
            high_dvec2 = !high_dvec2;
         } else {
            offset = nir_iadd_imm(b, offset, slot_size);
         }
      }

      return nir_vec(b, comp64, intrin->def.num_components);
   } else if (intrin->def.bit_size == 1) {
      /* Booleans are 32-bit */
      assert(glsl_type_is_boolean(type));
      return nir_b2b1(&state->builder,
                      emit_load(state, array_index, var, offset, component,
                                intrin->def.num_components, 32,
                                nir_type_bool32, false));
   } else {
      return emit_load(state, array_index, var, offset, component,
                       intrin->def.num_components,
                       intrin->def.bit_size,
                       nir_get_nir_type_for_glsl_type(type), false);
   }
}

static void
emit_store(struct lower_io_state *state, nir_def *data,
           nir_def *array_index, nir_variable *var, nir_def *offset,
           unsigned component, unsigned num_components,
           nir_component_mask_t write_mask, nir_alu_type src_type)
{
   nir_builder *b = &state->builder;

   nir_intrinsic_op op;
   if (var->data.mode == nir_var_mem_pixel_local_out ||
       var->data.mode == nir_var_mem_pixel_local_inout)
      op = nir_intrinsic_store_pixel_local;
   else if (!array_index)
      op = nir_intrinsic_store_output;
   else if (var->data.per_view)
      op = nir_intrinsic_store_per_view_output;
   else if (var->data.per_primitive)
      op = nir_intrinsic_store_per_primitive_output;
   else
      op = nir_intrinsic_store_per_vertex_output;

   nir_intrinsic_instr *store =
      nir_intrinsic_instr_create(state->builder.shader, op);
   store->num_components = num_components;
   store->name = add_variable_name(state, var->name);

   store->src[0] = nir_src_for_ssa(data);

   const struct glsl_type *type = var->type;
   if (array_index)
      type = glsl_get_array_element(type);
   unsigned var_size = state->type_size(type, var->data.bindless);
   nir_intrinsic_set_base(store, var->data.driver_location);
   nir_intrinsic_set_range(store, var_size);
   nir_intrinsic_set_component(store, component);
   nir_intrinsic_set_src_type(store, src_type);

   nir_intrinsic_set_write_mask(store, write_mask);

   if (nir_intrinsic_has_access(store))
      nir_intrinsic_set_access(store, var->data.access);

   if (array_index)
      store->src[1] = nir_src_for_ssa(array_index);

   unsigned num_slots = get_number_of_slots(state, var);

   offset = simplify_offset_src(b, offset, num_slots);
   store->src[array_index ? 2 : 1] = nir_src_for_ssa(offset);

   unsigned gs_streams = 0;
   if (state->builder.shader->info.stage == MESA_SHADER_GEOMETRY) {
      if (var->data.stream & NIR_STREAM_PACKED) {
         gs_streams = var->data.stream & ~NIR_STREAM_PACKED;
      } else {
         assert(var->data.stream < 4);
         gs_streams = 0;
         for (unsigned i = 0; i < num_components; ++i)
            gs_streams |= var->data.stream << (2 * i);
      }
   }

   int location = var->data.location;

   /* Maximum values in nir_io_semantics. */
   assert(num_slots <= 63);
   assert(location >= 0 && location + num_slots <= NUM_TOTAL_VARYING_SLOTS);

   nir_io_semantics semantics = { 0 };
   semantics.location = location;
   semantics.num_slots = num_slots;
   semantics.dual_source_blend_index = var->data.index;
   semantics.gs_streams = gs_streams;
   semantics.medium_precision = is_medium_precision(b->shader, var);
   semantics.per_view = var->data.per_view;

   nir_intrinsic_set_io_semantics(store, semantics);
   if (op == nir_intrinsic_store_pixel_local) {
      assert(var && var->data.image.format != PIPE_FORMAT_NONE);
      nir_intrinsic_set_format(store, var->data.image.format);
   }

   nir_builder_instr_insert(b, &store->instr);
}

static void
lower_store(nir_intrinsic_instr *intrin, struct lower_io_state *state,
            nir_def *array_index, nir_variable *var, nir_def *offset,
            unsigned component, const struct glsl_type *type)
{
   const bool lower_double = !glsl_type_is_integer(type) && state->options & nir_lower_io_lower_64bit_float_to_32;
   if (intrin->src[1].ssa->bit_size == 64 &&
       (lower_double || (state->options & (nir_lower_io_lower_64bit_to_32 |
                                           nir_lower_io_lower_64bit_to_32_new)))) {
      nir_builder *b = &state->builder;

      const unsigned slot_size = state->type_size(glsl_dvec_type(2), false);

      assert(component == 0 || component == 2);
      unsigned src_comp = 0;
      nir_component_mask_t write_mask = nir_intrinsic_write_mask(intrin);
      while (src_comp < intrin->num_components) {
         const unsigned num_comps =
            MIN2(intrin->num_components - src_comp,
                 (4 - component) / 2);

         if (write_mask & BITFIELD_MASK(num_comps)) {
            nir_def *data =
               nir_channels(b, intrin->src[1].ssa,
                            BITFIELD_RANGE(src_comp, num_comps));
            nir_def *data32 = nir_bitcast_vector(b, data, 32);

            uint32_t write_mask32 = 0;
            for (unsigned i = 0; i < num_comps; i++) {
               if (write_mask & BITFIELD_MASK(num_comps) & (1 << i))
                  write_mask32 |= 3 << (i * 2);
            }

            emit_store(state, data32, array_index, var, offset,
                       component, data32->num_components, write_mask32,
                       nir_type_uint32);
         }

         /* Only the first store has a component offset */
         component = 0;
         src_comp += num_comps;
         write_mask >>= num_comps;
         offset = nir_iadd_imm(b, offset, slot_size);
      }
   } else if (intrin->src[1].ssa->bit_size == 1) {
      /* Booleans are 32-bit */
      assert(glsl_type_is_boolean(type));
      nir_def *b32_val = nir_b2b32(&state->builder, intrin->src[1].ssa);
      emit_store(state, b32_val, array_index, var, offset,
                 component, intrin->num_components,
                 nir_intrinsic_write_mask(intrin),
                 nir_type_bool32);
   } else {
      emit_store(state, intrin->src[1].ssa, array_index, var, offset,
                 component, intrin->num_components,
                 nir_intrinsic_write_mask(intrin),
                 nir_get_nir_type_for_glsl_type(type));
   }
}

static nir_def *
lower_interpolate_at(nir_intrinsic_instr *intrin, struct lower_io_state *state,
                     nir_variable *var, nir_def *offset, unsigned component,
                     const struct glsl_type *type)
{
   nir_builder *b = &state->builder;
   assert(var->data.mode == nir_var_shader_in);

   /* Ignore interpolateAt() for flat variables - flat is flat. Lower
    * interpolateAtVertex() for explicit variables.
    */
   if (var->data.interpolation == INTERP_MODE_FLAT ||
       var->data.interpolation == INTERP_MODE_EXPLICIT) {
      nir_def *vertex_index = NULL;

      if (var->data.interpolation == INTERP_MODE_EXPLICIT) {
         assert(intrin->intrinsic == nir_intrinsic_interp_deref_at_vertex);
         vertex_index = intrin->src[1].ssa;
      }

      return lower_load(intrin, state, vertex_index, var, offset, component, type);
   }

   /* None of the supported APIs allow interpolation on 64-bit things */
   assert(intrin->def.bit_size <= 32);

   nir_intrinsic_op bary_op;
   switch (intrin->intrinsic) {
   case nir_intrinsic_interp_deref_at_centroid:
      bary_op = nir_intrinsic_load_barycentric_centroid;
      break;
   case nir_intrinsic_interp_deref_at_sample:
      bary_op = nir_intrinsic_load_barycentric_at_sample;
      break;
   case nir_intrinsic_interp_deref_at_offset:
      bary_op = nir_intrinsic_load_barycentric_at_offset;
      break;
   default:
      UNREACHABLE("Bogus interpolateAt() intrinsic.");
   }

   nir_intrinsic_instr *bary_setup =
      nir_intrinsic_instr_create(state->builder.shader, bary_op);

   nir_def_init(&bary_setup->instr, &bary_setup->def, 2, 32);
   nir_intrinsic_set_interp_mode(bary_setup, get_interp_mode(var));

   if (intrin->intrinsic == nir_intrinsic_interp_deref_at_sample ||
       intrin->intrinsic == nir_intrinsic_interp_deref_at_offset ||
       intrin->intrinsic == nir_intrinsic_interp_deref_at_vertex)
      bary_setup->src[0] = nir_src_for_ssa(intrin->src[1].ssa);

   nir_builder_instr_insert(b, &bary_setup->instr);

   nir_io_semantics semantics = { 0 };
   semantics.location = var->data.location;
   semantics.num_slots = get_number_of_slots(state, var);
   semantics.medium_precision = is_medium_precision(b->shader, var);

   offset = simplify_offset_src(b, offset, semantics.num_slots);

   nir_def *load =
      nir_load_interpolated_input(&state->builder,
                                  intrin->def.num_components,
                                  intrin->def.bit_size,
                                  &bary_setup->def,
                                  offset,
                                  .base = var->data.driver_location,
                                  .component = component,
                                  .io_semantics = semantics);

   return load;
}

/**
 * Convert a compact view index emitted by nir_lower_multiview to an absolute
 * view index.
 */
static nir_def *
uncompact_view_index(nir_builder *b, nir_src compact_index_src)
{
   /* We require nir_lower_io_vars_to_temporaries when using absolute view indices,
    * which ensures index is constant */
   assert(nir_src_is_const(compact_index_src));
   unsigned compact_index = nir_src_as_uint(compact_index_src);

   unsigned view_index;
   uint32_t view_mask = b->shader->info.view_mask;
   for (unsigned i = 0; i <= compact_index; i++) {
      view_index = u_bit_scan(&view_mask);
   }

   return nir_imm_int(b, view_index);
}

static bool
nir_lower_io_block(nir_block *block,
                   struct lower_io_state *state)
{
   nir_builder *b = &state->builder;
   const nir_shader_compiler_options *options = b->shader->options;
   bool progress = false;

   nir_foreach_instr_safe(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

      switch (intrin->intrinsic) {
      case nir_intrinsic_load_deref:
      case nir_intrinsic_store_deref:
         /* We can lower the io for this nir instrinsic */
         break;
      case nir_intrinsic_interp_deref_at_centroid:
      case nir_intrinsic_interp_deref_at_sample:
      case nir_intrinsic_interp_deref_at_offset:
      case nir_intrinsic_interp_deref_at_vertex:
         /* We can optionally lower these to load_interpolated_input */
         if (state->options & nir_lower_io_use_interpolated_input_intrinsics ||
             options->lower_interpolate_at)
            break;
         FALLTHROUGH;
      default:
         /* We can't lower the io for this nir instrinsic, so skip it */
         continue;
      }

      nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
      if (!nir_deref_mode_is_one_of(deref, state->modes))
         continue;

      nir_variable *var = nir_deref_instr_get_variable(deref);

      b->cursor = nir_before_instr(instr);

      const bool is_arrayed = nir_is_arrayed_io(var, b->shader->info.stage);

      nir_def *offset;
      nir_def *array_index = NULL;
      unsigned component_offset = var->data.location_frac;
      bool bindless_type_size = var->data.mode == nir_var_shader_in ||
                                var->data.mode == nir_var_shader_out ||
                                var->data.bindless;

      if (nir_deref_instr_is_known_out_of_bounds(deref)) {
         /* Section 5.11 (Out-of-Bounds Accesses) of the GLSL 4.60 spec says:
          *
          *    In the subsections described above for array, vector, matrix and
          *    structure accesses, any out-of-bounds access produced undefined
          *    behavior....
          *    Out-of-bounds reads return undefined values, which
          *    include values from other variables of the active program or zero.
          *    Out-of-bounds writes may be discarded or overwrite
          *    other variables of the active program.
          *
          * GL_KHR_robustness and GL_ARB_robustness encourage us to return zero
          * for reads.
          *
          * Otherwise get_io_offset would return out-of-bound offset which may
          * result in out-of-bound loading/storing of inputs/outputs,
          * that could cause issues in drivers down the line.
          */
         if (intrin->intrinsic != nir_intrinsic_store_deref) {
            nir_def *zero =
               nir_imm_zero(b, intrin->def.num_components,
                            intrin->def.bit_size);
            nir_def_rewrite_uses(&intrin->def,
                                 zero);
         }

         nir_instr_remove(&intrin->instr);
         progress = true;
         continue;
      }

      offset = get_io_offset(b, deref, is_arrayed ? &array_index : NULL,
                             state->type_size, &component_offset,
                             bindless_type_size);

      if (!options->compact_view_index && array_index && var->data.per_view)
         array_index = uncompact_view_index(b, nir_src_for_ssa(array_index));

      nir_def *replacement = NULL;

      switch (intrin->intrinsic) {
      case nir_intrinsic_load_deref:
         replacement = lower_load(intrin, state, array_index, var, offset,
                                  component_offset, deref->type);
         break;

      case nir_intrinsic_store_deref:
         lower_store(intrin, state, array_index, var, offset,
                     component_offset, deref->type);
         break;

      case nir_intrinsic_interp_deref_at_centroid:
      case nir_intrinsic_interp_deref_at_sample:
      case nir_intrinsic_interp_deref_at_offset:
      case nir_intrinsic_interp_deref_at_vertex:
         assert(array_index == NULL);
         replacement = lower_interpolate_at(intrin, state, var, offset,
                                            component_offset, deref->type);
         break;

      default:
         continue;
      }

      if (replacement) {
         nir_def_rewrite_uses(&intrin->def,
                              replacement);
      }
      nir_instr_remove(&intrin->instr);
      progress = true;
   }

   return progress;
}

static bool
nir_lower_io_impl(nir_function_impl *impl,
                  nir_variable_mode modes,
                  int (*type_size)(const struct glsl_type *, bool),
                  nir_lower_io_options options)
{
   struct lower_io_state state;
   bool progress = false;

   state.builder = nir_builder_create(impl);
   state.dead_ctx = ralloc_context(NULL);
   state.modes = modes;
   state.type_size = type_size;
   state.options = options;
   _mesa_set_init(&state.variable_names, state.dead_ctx,
                  _mesa_hash_string, _mesa_key_string_equal);

   ASSERTED nir_variable_mode supported_modes =
      nir_var_shader_in | nir_var_shader_out | nir_var_uniform |
      nir_var_any_pixel_local;
   assert(!(modes & ~supported_modes));

   nir_foreach_block(block, impl) {
      progress |= nir_lower_io_block(block, &state);
   }

   ralloc_free(state.dead_ctx);

   nir_progress(true, impl, nir_metadata_none);

   return progress;
}

/** Lower load/store_deref intrinsics on I/O variables to offset-based intrinsics
 *
 * This pass is intended to be used for cross-stage shader I/O and driver-
 * managed uniforms to turn deref-based access into a simpler model using
 * locations or offsets.  For fragment shader inputs, it can optionally turn
 * load_deref into an explicit interpolation using barycentrics coming from
 * one of the load_barycentric_* intrinsics.  This pass requires that all
 * deref chains are complete and contain no casts.
 */
bool
nir_lower_io(nir_shader *shader, nir_variable_mode modes,
             int (*type_size)(const struct glsl_type *, bool),
             nir_lower_io_options options)
{
   bool progress = false;

   nir_foreach_function_impl(impl, shader) {
      progress |= nir_lower_io_impl(impl, modes, type_size, options);
   }

   return progress;
}

/**
 * Return the offset source number for a load/store intrinsic or -1 if there's no offset.
 */
int
nir_get_io_offset_src_number(const nir_intrinsic_instr *instr)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_load_input:
   case nir_intrinsic_load_per_primitive_input:
   case nir_intrinsic_load_output:
   case nir_intrinsic_load_pixel_local:
   case nir_intrinsic_load_shared:
   case nir_intrinsic_load_task_payload:
   case nir_intrinsic_load_uniform:
   case nir_intrinsic_load_constant:
   case nir_intrinsic_load_push_constant:
   case nir_intrinsic_load_kernel_input:
   case nir_intrinsic_load_global:
   case nir_intrinsic_load_global_2x32:
   case nir_intrinsic_load_global_constant:
   case nir_intrinsic_load_global_etna:
   case nir_intrinsic_load_scratch:
   case nir_intrinsic_load_fs_input_interp_deltas:
   case nir_intrinsic_shared_atomic:
   case nir_intrinsic_shared_atomic_swap:
   case nir_intrinsic_task_payload_atomic:
   case nir_intrinsic_task_payload_atomic_swap:
   case nir_intrinsic_global_atomic:
   case nir_intrinsic_global_atomic_2x32:
   case nir_intrinsic_global_atomic_swap:
   case nir_intrinsic_global_atomic_swap_2x32:
   case nir_intrinsic_load_coefficients_agx:
   case nir_intrinsic_load_shared_block_intel:
   case nir_intrinsic_load_global_block_intel:
   case nir_intrinsic_load_shared_uniform_block_intel:
   case nir_intrinsic_load_global_constant_uniform_block_intel:
   case nir_intrinsic_load_shared2_amd:
   case nir_intrinsic_load_const_ir3:
   case nir_intrinsic_load_shared_ir3:
      return 0;
   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_ubo_vec4:
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_load_input_vertex:
   case nir_intrinsic_load_per_vertex_input:
   case nir_intrinsic_load_per_vertex_output:
   case nir_intrinsic_load_per_view_output:
   case nir_intrinsic_load_per_primitive_output:
   case nir_intrinsic_load_interpolated_input:
   case nir_intrinsic_load_global_amd:
   case nir_intrinsic_store_output:
   case nir_intrinsic_store_pixel_local:
   case nir_intrinsic_store_shared:
   case nir_intrinsic_store_task_payload:
   case nir_intrinsic_store_global:
   case nir_intrinsic_store_global_2x32:
   case nir_intrinsic_store_global_etna:
   case nir_intrinsic_store_scratch:
   case nir_intrinsic_ssbo_atomic:
   case nir_intrinsic_ssbo_atomic_swap:
   case nir_intrinsic_ldc_nv:
   case nir_intrinsic_ldcx_nv:
   case nir_intrinsic_load_ssbo_block_intel:
   case nir_intrinsic_store_global_block_intel:
   case nir_intrinsic_store_shared_block_intel:
   case nir_intrinsic_load_ubo_uniform_block_intel:
   case nir_intrinsic_load_ssbo_uniform_block_intel:
   case nir_intrinsic_load_buffer_amd:
   case nir_intrinsic_store_shared2_amd:
   case nir_intrinsic_store_shared_ir3:
   case nir_intrinsic_load_ssbo_intel:
      return 1;
   case nir_intrinsic_store_ssbo:
   case nir_intrinsic_store_per_vertex_output:
   case nir_intrinsic_store_per_view_output:
   case nir_intrinsic_store_per_primitive_output:
   case nir_intrinsic_load_attribute_pan:
   case nir_intrinsic_store_ssbo_block_intel:
   case nir_intrinsic_store_buffer_amd:
   case nir_intrinsic_store_ssbo_intel:
   case nir_intrinsic_store_global_amd:
   case nir_intrinsic_global_atomic_amd:
      return 2;
   case nir_intrinsic_load_ssbo_ir3:
      /* This intrinsic has 2 offsets (src1 bytes, src2 dwords), we return the
       * dwords one for opt_offsets.
       */
      return 2;
   case nir_intrinsic_store_ssbo_ir3:
      /* This intrinsic has 2 offsets (src2 bytes, src3 dwords), we return the
       * dwords one for opt_offsets.
       */
      return 3;
   case nir_intrinsic_global_atomic_swap_amd:
      return 3;
   default:
      return -1;
   }
}

/**
 * Return the offset source for a load/store intrinsic.
 */
nir_src *
nir_get_io_offset_src(nir_intrinsic_instr *instr)
{
   const int idx = nir_get_io_offset_src_number(instr);
   return idx >= 0 ? &instr->src[idx] : NULL;
}

/**
 * Return the index or handle source number for a load/store intrinsic or -1
 * if there's no index or handle.
 */
int
nir_get_io_index_src_number(const nir_intrinsic_instr *instr)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_get_ubo_size:
   case nir_intrinsic_get_ssbo_size:
   case nir_intrinsic_load_input_vertex:
   case nir_intrinsic_load_per_vertex_input:
   case nir_intrinsic_load_per_vertex_output:
   case nir_intrinsic_load_per_view_output:
   case nir_intrinsic_load_per_primitive_output:
   case nir_intrinsic_load_interpolated_input:
   case nir_intrinsic_load_global_amd:
   case nir_intrinsic_global_atomic_amd:
   case nir_intrinsic_global_atomic_swap_amd:
   case nir_intrinsic_ldc_nv:
   case nir_intrinsic_ldcx_nv:
   case nir_intrinsic_load_ssbo_intel:
   case nir_intrinsic_load_ssbo_block_intel:
   case nir_intrinsic_store_global_block_intel:
   case nir_intrinsic_store_shared_block_intel:
   case nir_intrinsic_load_ubo_uniform_block_intel:
   case nir_intrinsic_load_ssbo_uniform_block_intel:
#define IMG_CASE(name) case nir_intrinsic_image_##name: case nir_intrinsic_bindless_image_##name
   IMG_CASE(load):
   IMG_CASE(store):
   IMG_CASE(sparse_load):
   IMG_CASE(atomic):
   IMG_CASE(atomic_swap):
   IMG_CASE(size):
   IMG_CASE(levels):
   IMG_CASE(samples):
   IMG_CASE(texel_address):
   IMG_CASE(samples_identical):
   IMG_CASE(descriptor_amd):
   IMG_CASE(format):
   IMG_CASE(order):
   IMG_CASE(fragment_mask_load_amd):
      return 0;
#undef IMG_CASE
   case nir_intrinsic_store_ssbo:
   case nir_intrinsic_store_per_vertex_output:
   case nir_intrinsic_store_per_view_output:
   case nir_intrinsic_store_per_primitive_output:
   case nir_intrinsic_store_ssbo_block_intel:
   case nir_intrinsic_store_ssbo_intel:
   case nir_intrinsic_store_global_amd:
      return 1;
   default:
      return -1;
   }
}

/**
 * Return the offset or handle source for a load/store intrinsic.
 */
nir_src *
nir_get_io_index_src(nir_intrinsic_instr *instr)
{
   const int idx = nir_get_io_index_src_number(instr);
   return idx >= 0 ? &instr->src[idx] : NULL;
}

/**
 * Return the array index source number for an arrayed load/store intrinsic or -1 if there's no offset.
 */
int
nir_get_io_arrayed_index_src_number(const nir_intrinsic_instr *instr)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_load_per_vertex_input:
   case nir_intrinsic_load_per_vertex_output:
   case nir_intrinsic_load_per_view_output:
   case nir_intrinsic_load_per_primitive_output:
      return 0;
   case nir_intrinsic_store_per_vertex_output:
   case nir_intrinsic_store_per_view_output:
   case nir_intrinsic_store_per_primitive_output:
      return 1;
   default:
      return -1;
   }
}

bool
nir_is_output_load(nir_intrinsic_instr *intr)
{
   return intr->intrinsic == nir_intrinsic_load_output ||
          intr->intrinsic == nir_intrinsic_load_per_vertex_output ||
          intr->intrinsic == nir_intrinsic_load_per_primitive_output ||
          intr->intrinsic == nir_intrinsic_load_per_view_output;
}

bool
nir_is_input_load(nir_intrinsic_instr *intr)
{
   return intr->intrinsic == nir_intrinsic_load_input ||
          intr->intrinsic == nir_intrinsic_load_per_vertex_input ||
          intr->intrinsic == nir_intrinsic_load_per_primitive_input ||
          intr->intrinsic == nir_intrinsic_load_interpolated_input ||
          intr->intrinsic == nir_intrinsic_load_input_vertex ||
          intr->intrinsic == nir_intrinsic_load_fs_input_interp_deltas;
}

/**
 * Return the array index source for an arrayed load/store intrinsic.
 */
nir_src *
nir_get_io_arrayed_index_src(nir_intrinsic_instr *instr)
{
   const int idx = nir_get_io_arrayed_index_src_number(instr);
   return idx >= 0 ? &instr->src[idx] : NULL;
}

static int
type_size_vec4(const struct glsl_type *type, bool bindless)
{
   return glsl_count_attribute_slots(type, false);
}

/**
 * This runs all compiler passes needed to lower IO, lower indirect IO access,
 * set transform feedback info in IO intrinsics, and clean up the IR.
 *
 * \param renumber_vs_inputs
 *    Set to true if holes between VS inputs should be removed, which is safe
 *    to do in any shader linker that can handle that. Set to false if you want
 *    to keep holes between VS inputs, which is recommended to do in gallium
 *    drivers so as not to break the mapping of vertex elements to VS inputs
 *    expected by gallium frontends.
 */
void
nir_lower_io_passes(nir_shader *nir, bool renumber_vs_inputs)
{
   if (mesa_shader_stage_is_compute(nir->info.stage) ||
       nir->info.stage == MESA_SHADER_TASK)
      return;

   /* If the driver doesn't support indirect TCS output slot access, lower
    * it to an if-else tree of direct accesses.
    */
   if (nir->info.stage == MESA_SHADER_TESS_CTRL &&
       !(nir->options->support_indirect_outputs &
         BITFIELD_BIT(nir->info.stage))) {
      NIR_PASS(_, nir, nir_lower_indirect_derefs_to_if_else_trees,
               nir_var_shader_out, UINT32_MAX);
   }

   /* For VS, TES, GS, FS: Always lower all outputs to temps, which:
    * - lowers output loads (nobody expects those outside of TCS & MS)
    * - lowers indirect output slot indexing (also XFB info can't be stored
    *   in indirect IO intrinsics)
    * - moves output stores to the end or GS emits
    */
   if (nir->info.stage == MESA_SHADER_VERTEX ||
       nir->info.stage == MESA_SHADER_TESS_EVAL ||
       nir->info.stage == MESA_SHADER_GEOMETRY ||
       nir->info.stage == MESA_SHADER_FRAGMENT) {
      /* TODO: Sorting variables by location is required due to some bug
       * in nir_lower_io_vars_to_temporaries. If variables are not sorted,
       * dEQP-GLES31.functional.separate_shader.random.0 fails.
       */
      unsigned varying_var_mask =
         (nir->info.stage != MESA_SHADER_VERTEX ? nir_var_shader_in : 0) |
         (nir->info.stage != MESA_SHADER_FRAGMENT ? nir_var_shader_out : 0);
      nir_sort_variables_by_location(nir, varying_var_mask);

      NIR_PASS(_, nir, nir_lower_io_vars_to_temporaries,
               nir_shader_get_entrypoint(nir), nir_var_shader_out);

      /* We need to lower all the copy_deref's introduced by lower_io_to-
       * _temporaries before calling nir_lower_io.
       */
      NIR_PASS(_, nir, nir_split_var_copies);
      NIR_PASS(_, nir, nir_lower_var_copies);
      NIR_PASS(_, nir, nir_lower_global_vars_to_local);
   }

   /* The correct lower_64bit_to_32 flag is required by st/mesa depending
    * on whether the GLSL linker lowers IO or not. Setting the wrong flag
    * would break 64-bit vertex attribs for GLSL.
    */
   NIR_PASS(_, nir, nir_lower_io, nir_var_shader_out | nir_var_shader_in,
            type_size_vec4,
            (renumber_vs_inputs ? nir_lower_io_lower_64bit_to_32_new : nir_lower_io_lower_64bit_to_32) |
               nir_lower_io_use_interpolated_input_intrinsics);

   /* Fold constant offset srcs for IO. */
   NIR_PASS(_, nir, nir_opt_constant_folding);

   /* This must be called after folding constant offset srcs. */
   if (nir->info.stage != MESA_SHADER_MESH &&
       !(nir->options->support_indirect_inputs & BITFIELD_BIT(nir->info.stage)))
      NIR_PASS(_, nir, nir_lower_io_indirect_loads, nir_var_shader_in);

   /* Lower and remove dead derefs and variables to clean up the IR. */
   NIR_PASS(_, nir, nir_lower_vars_to_ssa);
   NIR_PASS(_, nir, nir_opt_dce);
   NIR_PASS(_, nir, nir_remove_dead_variables, nir_var_function_temp, NULL);

   /* Output stores can have undef values. Eliminate those before
    * nir_recompute_io_bases. This happens with separate shaders, which are
    * usually not optimized further after this.
    */
   NIR_PASS(_, nir, nir_opt_undef);

   /* If IO is lowered before var->data.driver_location is assigned, driver
    * locations are all 0, which means IO bases are all 0. It's not necessary
    * to set driver_location before lowering IO because the only thing that
    * identifies outputs is their semantic, and IO bases can always be
    * computed from the semantics.
    *
    * This assigns IO bases from scratch, using IO semantics to tell which
    * intrinsics refer to the same IO. If the bases already exist, they
    * will be reassigned, sorted by the semantic, and all holes removed.
    * This kind of canonicalizes all bases.
    *
    * This must be done after DCE to remove dead load_input intrinsics.
    */
   bool recompute_inputs =
      (nir->info.stage != MESA_SHADER_VERTEX || renumber_vs_inputs) &&
      nir->info.stage != MESA_SHADER_MESH;
   NIR_PASS(_, nir, nir_recompute_io_bases,
            (recompute_inputs ? nir_var_shader_in : 0) | nir_var_shader_out);

   if (nir->xfb_info)
      NIR_PASS(_, nir, nir_io_add_intrinsic_xfb_info);

   if (nir->options->lower_mediump_io)
      nir->options->lower_mediump_io(nir);

   nir->info.io_lowered = true;
}
