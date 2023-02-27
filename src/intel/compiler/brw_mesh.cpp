/*
 * Copyright © 2021 Intel Corporation
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

#include "brw_compiler.h"
#include "brw_fs.h"
#include "brw_nir.h"
#include "brw_private.h"
#include "compiler/nir/nir_builder.h"
#include "dev/intel_debug.h"

#include <memory>

using namespace brw;

static bool
brw_nir_lower_load_uniforms_filter(const nir_instr *instr,
                                   UNUSED const void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;
   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   return intrin->intrinsic == nir_intrinsic_load_uniform;
}

static nir_ssa_def *
brw_nir_lower_load_uniforms_impl(nir_builder *b, nir_instr *instr,
                                 UNUSED void *data)
{
   assert(instr->type == nir_instr_type_intrinsic);
   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   assert(intrin->intrinsic == nir_intrinsic_load_uniform);

   /* Read the first few 32-bit scalars from InlineData. */
   if (nir_src_is_const(intrin->src[0]) &&
       nir_dest_bit_size(intrin->dest) == 32 &&
       nir_dest_num_components(intrin->dest) == 1) {
      unsigned off = nir_intrinsic_base(intrin) + nir_src_as_uint(intrin->src[0]);
      unsigned off_dw = off / 4;
      if (off % 4 == 0 && off_dw < BRW_TASK_MESH_PUSH_CONSTANTS_SIZE_DW) {
         off_dw += BRW_TASK_MESH_PUSH_CONSTANTS_START_DW;
         return nir_load_mesh_inline_data_intel(b, 32, off_dw);
      }
   }

   return brw_nir_load_global_const(b, intrin,
                                    nir_load_mesh_inline_data_intel(b, 64, 0), 0);
}

static bool
brw_nir_lower_load_uniforms(nir_shader *nir)
{
   return nir_shader_lower_instructions(nir, brw_nir_lower_load_uniforms_filter,
                                        brw_nir_lower_load_uniforms_impl, NULL);
}

static inline int
type_size_scalar_dwords(const struct glsl_type *type, bool bindless)
{
   return glsl_count_dword_slots(type, bindless);
}

/* TODO(mesh): Make this a common function. */
static void
shared_type_info(const struct glsl_type *type, unsigned *size, unsigned *align)
{
   assert(glsl_type_is_vector_or_scalar(type));

   uint32_t comp_size = glsl_type_is_boolean(type)
      ? 4 : glsl_get_bit_size(type) / 8;
   unsigned length = glsl_get_vector_elements(type);
   *size = comp_size * length,
   *align = comp_size * (length == 3 ? 4 : length);
}

static bool
brw_nir_lower_launch_mesh_workgroups_instr(nir_builder *b, nir_instr *instr, void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   if (intrin->intrinsic != nir_intrinsic_launch_mesh_workgroups)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);

   nir_ssa_def *local_invocation_index = nir_load_local_invocation_index(b);

   /* Make sure that the mesh workgroup size is taken from the first invocation
    * (nir_intrinsic_launch_mesh_workgroups requirement)
    */
   nir_ssa_def *cmp = nir_ieq(b, local_invocation_index, nir_imm_int(b, 0));
   nir_if *if_stmt = nir_push_if(b, cmp);
   {
      /* TUE header contains 4 words:
       *
       * - Word 0 for Task Count.
       *
       * - Words 1-3 used for "Dispatch Dimensions" feature, to allow mapping a
       *   3D dispatch into the 1D dispatch supported by HW.
       */
      nir_ssa_def *x = nir_channel(b, intrin->src[0].ssa, 0);
      nir_ssa_def *y = nir_channel(b, intrin->src[0].ssa, 1);
      nir_ssa_def *z = nir_channel(b, intrin->src[0].ssa, 2);
      nir_ssa_def *task_count = nir_imul(b, x, nir_imul(b, y, z));
      nir_ssa_def *tue_header = nir_vec4(b, task_count, x, y, z);
      nir_store_task_payload(b, tue_header, nir_imm_int(b, 0));
   }
   nir_pop_if(b, if_stmt);

   nir_instr_remove(instr);

   return true;
}

static bool
brw_nir_lower_launch_mesh_workgroups(nir_shader *nir)
{
   return nir_shader_instructions_pass(nir,
                                       brw_nir_lower_launch_mesh_workgroups_instr,
                                       nir_metadata_none,
                                       NULL);
}

static void
brw_nir_lower_tue_outputs(nir_shader *nir, brw_tue_map *map)
{
   memset(map, 0, sizeof(*map));

   NIR_PASS(_, nir, nir_lower_io, nir_var_shader_out,
            type_size_scalar_dwords, nir_lower_io_lower_64bit_to_32);

   /* From bspec: "It is suggested that SW reserve the 16 bytes following the
    * TUE Header, and therefore start the SW-defined data structure at 32B
    * alignment.  This allows the TUE Header to always be written as 32 bytes
    * with 32B alignment, the most optimal write performance case."
    */
   map->per_task_data_start_dw = 8;

   /* Lowering to explicit types will start offsets from task_payload_size, so
    * set it to start after the header.
    */
   nir->info.task_payload_size = map->per_task_data_start_dw * 4;
   NIR_PASS(_, nir, nir_lower_vars_to_explicit_types,
            nir_var_mem_task_payload, shared_type_info);
   NIR_PASS(_, nir, nir_lower_explicit_io,
            nir_var_mem_task_payload, nir_address_format_32bit_offset);

   map->size_dw = ALIGN(DIV_ROUND_UP(nir->info.task_payload_size, 4), 8);
}

static void
brw_print_tue_map(FILE *fp, const struct brw_tue_map *map)
{
   fprintf(fp, "TUE (%d dwords)\n\n", map->size_dw);
}

static bool
brw_nir_adjust_task_payload_offsets_instr(struct nir_builder *b,
                                          nir_instr *instr, void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   switch (intrin->intrinsic) {
   case nir_intrinsic_store_task_payload:
   case nir_intrinsic_load_task_payload: {
      nir_src *offset_src = nir_get_io_offset_src(intrin);

      if (nir_src_is_const(*offset_src))
         assert(nir_src_as_uint(*offset_src) % 4 == 0);

      b->cursor = nir_before_instr(&intrin->instr);

      /* Regular I/O uses dwords while explicit I/O used for task payload uses
       * bytes.  Normalize it to dwords.
       *
       * TODO(mesh): Figure out how to handle 8-bit, 16-bit.
       */

      assert(offset_src->is_ssa);
      nir_ssa_def *offset = nir_ishr_imm(b, offset_src->ssa, 2);
      nir_instr_rewrite_src(&intrin->instr, offset_src, nir_src_for_ssa(offset));

      unsigned base = nir_intrinsic_base(intrin);
      assert(base % 4 == 0);
      nir_intrinsic_set_base(intrin, base / 4);

      return true;
   }

   default:
      return false;
   }
}

static bool
brw_nir_adjust_task_payload_offsets(nir_shader *nir)
{
   return nir_shader_instructions_pass(nir,
                                       brw_nir_adjust_task_payload_offsets_instr,
                                       nir_metadata_block_index |
                                       nir_metadata_dominance,
                                       NULL);
}

static void
brw_nir_adjust_payload(nir_shader *shader, const struct brw_compiler *compiler)
{
   /* Adjustment of task payload offsets must be performed *after* last pass
    * which interprets them as bytes, because it changes their unit.
    */
   bool adjusted = false;
   NIR_PASS(adjusted, shader, brw_nir_adjust_task_payload_offsets);
   if (adjusted) /* clean up the mess created by offset adjustments */
      NIR_PASS(_, shader, nir_opt_constant_folding);
}

static bool
brw_nir_align_launch_mesh_workgroups_instr(nir_builder *b, nir_instr *instr, void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   if (intrin->intrinsic != nir_intrinsic_launch_mesh_workgroups)
      return false;

   /* nir_lower_task_shader uses "range" as task payload size. */
   unsigned range = nir_intrinsic_range(intrin);
   /* This will avoid special case in nir_lower_task_shader dealing with
    * not vec4-aligned payload when payload_in_shared workaround is enabled.
    */
   nir_intrinsic_set_range(intrin, ALIGN(range, 16));

   return true;
}

static bool
brw_nir_align_launch_mesh_workgroups(nir_shader *nir)
{
   return nir_shader_instructions_pass(nir,
                                       brw_nir_align_launch_mesh_workgroups_instr,
                                       nir_metadata_block_index |
                                       nir_metadata_dominance,
                                       NULL);
}

const unsigned *
brw_compile_task(const struct brw_compiler *compiler,
                 void *mem_ctx,
                 struct brw_compile_task_params *params)
{
   struct nir_shader *nir = params->nir;
   const struct brw_task_prog_key *key = params->key;
   struct brw_task_prog_data *prog_data = params->prog_data;
   const bool debug_enabled = INTEL_DEBUG(DEBUG_TASK);

   brw_nir_lower_tue_outputs(nir, &prog_data->map);

   NIR_PASS(_, nir, brw_nir_align_launch_mesh_workgroups);

   nir_lower_task_shader_options lower_ts_opt = {
      .payload_to_shared_for_atomics = true,
      .payload_to_shared_for_small_types = true,
      /* The actual payload data starts after the TUE header and padding,
       * so skip those when copying.
       */
      .payload_offset_in_bytes = prog_data->map.per_task_data_start_dw * 4,
   };
   NIR_PASS(_, nir, nir_lower_task_shader, lower_ts_opt);

   NIR_PASS(_, nir, brw_nir_lower_launch_mesh_workgroups);

   prog_data->base.base.stage = MESA_SHADER_TASK;
   prog_data->base.base.total_shared = nir->info.shared_size;
   prog_data->base.base.total_scratch = 0;

   prog_data->base.local_size[0] = nir->info.workgroup_size[0];
   prog_data->base.local_size[1] = nir->info.workgroup_size[1];
   prog_data->base.local_size[2] = nir->info.workgroup_size[2];

   prog_data->uses_drawid =
      BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_DRAW_ID);

   brw_simd_selection_state simd_state{
      .mem_ctx = mem_ctx,
      .devinfo = compiler->devinfo,
      .prog_data = &prog_data->base,
      .required_width = brw_required_dispatch_width(&nir->info),
   };

   std::unique_ptr<fs_visitor> v[3];

   for (unsigned simd = 0; simd < 3; simd++) {
      if (!brw_simd_should_compile(simd_state, simd))
         continue;

      const unsigned dispatch_width = 8 << simd;

      nir_shader *shader = nir_shader_clone(mem_ctx, nir);
      brw_nir_apply_key(shader, compiler, &key->base, dispatch_width, true /* is_scalar */);

      NIR_PASS(_, shader, brw_nir_lower_load_uniforms);
      NIR_PASS(_, shader, brw_nir_lower_simd, dispatch_width);

      brw_postprocess_nir(shader, compiler, true /* is_scalar */, debug_enabled,
                          key->base.robust_buffer_access);

      brw_nir_adjust_payload(shader, compiler);

      v[simd] = std::make_unique<fs_visitor>(compiler, params->log_data, mem_ctx, &key->base,
                                             &prog_data->base.base, shader, dispatch_width,
                                             debug_enabled);

      if (prog_data->base.prog_mask) {
         unsigned first = ffs(prog_data->base.prog_mask) - 1;
         v[simd]->import_uniforms(v[first].get());
      }

      const bool allow_spilling = !brw_simd_any_compiled(simd_state);
      if (v[simd]->run_task(allow_spilling))
         brw_simd_mark_compiled(simd_state, simd, v[simd]->spilled_any_registers);
      else
         simd_state.error[simd] = ralloc_strdup(mem_ctx, v[simd]->fail_msg);
   }

   int selected_simd = brw_simd_select(simd_state);
   if (selected_simd < 0) {
      params->error_str = ralloc_asprintf(mem_ctx, "Can't compile shader: %s, %s and %s.\n",
                                          simd_state.error[0], simd_state.error[1],
                                          simd_state.error[2]);
      return NULL;
   }

   fs_visitor *selected = v[selected_simd].get();
   prog_data->base.prog_mask = 1 << selected_simd;

   if (unlikely(debug_enabled)) {
      fprintf(stderr, "Task Output ");
      brw_print_tue_map(stderr, &prog_data->map);
   }

   fs_generator g(compiler, params->log_data, mem_ctx,
                  &prog_data->base.base, false, MESA_SHADER_TASK);
   if (unlikely(debug_enabled)) {
      g.enable_debug(ralloc_asprintf(mem_ctx,
                                     "%s task shader %s",
                                     nir->info.label ? nir->info.label
                                                     : "unnamed",
                                     nir->info.name));
   }

   g.generate_code(selected->cfg, selected->dispatch_width, selected->shader_stats,
                   selected->performance_analysis.require(), params->stats);
   g.add_const_data(nir->constant_data, nir->constant_data_size);
   return g.get_assembly();
}

static void
brw_nir_lower_tue_inputs(nir_shader *nir, const brw_tue_map *map)
{
   if (!map)
      return;

   nir->info.task_payload_size = map->per_task_data_start_dw * 4;

   bool progress = false;

   NIR_PASS(progress, nir, nir_lower_vars_to_explicit_types,
            nir_var_mem_task_payload, shared_type_info);

   if (progress) {
      /* The types for Task Output and Mesh Input should match, so their sizes
       * should also match.
       */
      assert(map->size_dw == ALIGN(DIV_ROUND_UP(nir->info.task_payload_size, 4), 8));
   } else {
      /* Mesh doesn't read any input, to make it clearer set the
       * task_payload_size to zero instead of keeping an incomplete size that
       * just includes the header.
       */
      nir->info.task_payload_size = 0;
   }

   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_task_payload,
            nir_address_format_32bit_offset);
}

/* Mesh URB Entry consists of an initial section
 *
 *  - Primitive Count
 *  - Primitive Indices (from 0 to Max-1)
 *  - Padding to 32B if needed
 *
 * optionally followed by a section for per-primitive data,
 * in which each primitive (from 0 to Max-1) gets
 *
 *  - Primitive Header (e.g. ViewportIndex)
 *  - Primitive Custom Attributes
 *
 * then followed by a section for per-vertex data
 *
 *  - Vertex Header (e.g. Position)
 *  - Vertex Custom Attributes
 *
 * Each per-element section has a pitch and a starting offset.  All the
 * individual attributes offsets in start_dw are considering the first entry
 * of the section (i.e. where the Position for first vertex, or ViewportIndex
 * for first primitive).  Attributes for other elements are calculated using
 * the pitch.
 */
static void
brw_compute_mue_map(struct nir_shader *nir, struct brw_mue_map *map)
{
   memset(map, 0, sizeof(*map));

   for (int i = 0; i < VARYING_SLOT_MAX; i++)
      map->start_dw[i] = -1;

   unsigned vertices_per_primitive =
      num_mesh_vertices_per_primitive(nir->info.mesh.primitive_type);

   map->max_primitives = nir->info.mesh.max_primitives_out;
   map->max_vertices = nir->info.mesh.max_vertices_out;

   uint64_t outputs_written = nir->info.outputs_written;

   /* Assign initial section. */
   if (BITFIELD64_BIT(VARYING_SLOT_PRIMITIVE_COUNT) & outputs_written) {
      map->start_dw[VARYING_SLOT_PRIMITIVE_COUNT] = 0;
      outputs_written &= ~BITFIELD64_BIT(VARYING_SLOT_PRIMITIVE_COUNT);
   }
   if (BITFIELD64_BIT(VARYING_SLOT_PRIMITIVE_INDICES) & outputs_written) {
      map->start_dw[VARYING_SLOT_PRIMITIVE_INDICES] = 1;
      outputs_written &= ~BITFIELD64_BIT(VARYING_SLOT_PRIMITIVE_INDICES);
   }

   /* One dword for primitives count then K extra dwords for each
    * primitive. Note this should change when we implement other index types.
    */
   const unsigned primitive_list_size_dw = 1 + vertices_per_primitive * map->max_primitives;

   /* TODO(mesh): Multiview. */
   map->per_primitive_header_size_dw =
         (nir->info.outputs_written & (BITFIELD64_BIT(VARYING_SLOT_VIEWPORT) |
                                       BITFIELD64_BIT(VARYING_SLOT_CULL_PRIMITIVE) |
                                       BITFIELD64_BIT(VARYING_SLOT_PRIMITIVE_SHADING_RATE) |
                                       BITFIELD64_BIT(VARYING_SLOT_LAYER))) ? 8 : 0;

   map->per_primitive_start_dw = ALIGN(primitive_list_size_dw, 8);

   map->per_primitive_data_size_dw = 0;
   u_foreach_bit64(location, outputs_written & nir->info.per_primitive_outputs) {
      assert(map->start_dw[location] == -1);

      unsigned start;
      switch (location) {
      case VARYING_SLOT_PRIMITIVE_SHADING_RATE:
         start = map->per_primitive_start_dw + 0;
         break;
      case VARYING_SLOT_LAYER:
         start = map->per_primitive_start_dw + 1; /* RTAIndex */
         break;
      case VARYING_SLOT_VIEWPORT:
         start = map->per_primitive_start_dw + 2;
         break;
      case VARYING_SLOT_CULL_PRIMITIVE:
         start = map->per_primitive_start_dw + 3;
         break;
      default:
         assert(location == VARYING_SLOT_PRIMITIVE_ID ||
                location >= VARYING_SLOT_VAR0);
         start = map->per_primitive_start_dw +
                 map->per_primitive_header_size_dw +
                 map->per_primitive_data_size_dw;
         map->per_primitive_data_size_dw += 4;
         break;
      }

      map->start_dw[location] = start;
   }

   map->per_primitive_pitch_dw = ALIGN(map->per_primitive_header_size_dw +
                                       map->per_primitive_data_size_dw, 8);

   map->per_vertex_start_dw = ALIGN(map->per_primitive_start_dw +
                                    map->per_primitive_pitch_dw * map->max_primitives, 8);

   /* TODO(mesh): Multiview. */
   unsigned fixed_header_size = 8;
   map->per_vertex_header_size_dw = ALIGN(fixed_header_size +
                                          nir->info.clip_distance_array_size +
                                          nir->info.cull_distance_array_size, 8);
   map->per_vertex_data_size_dw = 0;
   u_foreach_bit64(location, outputs_written & ~nir->info.per_primitive_outputs) {
      assert(map->start_dw[location] == -1);

      unsigned start;
      switch (location) {
      case VARYING_SLOT_PSIZ:
         start = map->per_vertex_start_dw + 3;
         break;
      case VARYING_SLOT_POS:
         start = map->per_vertex_start_dw + 4;
         break;
      case VARYING_SLOT_CLIP_DIST0:
         start = map->per_vertex_start_dw + fixed_header_size + 0;
         break;
      case VARYING_SLOT_CLIP_DIST1:
         start = map->per_vertex_start_dw + fixed_header_size + 4;
         break;
      case VARYING_SLOT_CULL_DIST0:
      case VARYING_SLOT_CULL_DIST1:
         unreachable("cull distances should be lowered earlier");
         break;
      default:
         assert(location >= VARYING_SLOT_VAR0);
         start = map->per_vertex_start_dw +
                 map->per_vertex_header_size_dw +
                 map->per_vertex_data_size_dw;
         map->per_vertex_data_size_dw += 4;
         break;
      }
      map->start_dw[location] = start;
   }

   map->per_vertex_pitch_dw = ALIGN(map->per_vertex_header_size_dw +
                                    map->per_vertex_data_size_dw, 8);

   map->size_dw =
      map->per_vertex_start_dw + map->per_vertex_pitch_dw * map->max_vertices;

   assert(map->size_dw % 8 == 0);
}

static void
brw_print_mue_map(FILE *fp, const struct brw_mue_map *map)
{
   fprintf(fp, "MUE map (%d dwords, %d primitives, %d vertices)\n",
           map->size_dw, map->max_primitives, map->max_vertices);
   fprintf(fp, "  %4d: VARYING_SLOT_PRIMITIVE_COUNT\n",
           map->start_dw[VARYING_SLOT_PRIMITIVE_COUNT]);
   fprintf(fp, "  %4d: VARYING_SLOT_PRIMITIVE_INDICES\n",
           map->start_dw[VARYING_SLOT_PRIMITIVE_INDICES]);

   fprintf(fp, "  ----- per primitive (start %d, header_size %d, data_size %d, pitch %d)\n",
           map->per_primitive_start_dw,
           map->per_primitive_header_size_dw,
           map->per_primitive_data_size_dw,
           map->per_primitive_pitch_dw);

   for (unsigned i = 0; i < VARYING_SLOT_MAX; i++) {
      if (map->start_dw[i] < 0)
         continue;
      const unsigned offset = map->start_dw[i];
      if (offset >= map->per_primitive_start_dw &&
          offset < map->per_primitive_start_dw + map->per_primitive_pitch_dw) {
         fprintf(fp, "  %4d: %s\n", offset,
                 gl_varying_slot_name_for_stage((gl_varying_slot)i,
                                                MESA_SHADER_MESH));
      }
   }

   fprintf(fp, "  ----- per vertex (start %d, header_size %d, data_size %d, pitch %d)\n",
           map->per_vertex_start_dw,
           map->per_vertex_header_size_dw,
           map->per_vertex_data_size_dw,
           map->per_vertex_pitch_dw);

   for (unsigned i = 0; i < VARYING_SLOT_MAX; i++) {
      if (map->start_dw[i] < 0)
         continue;
      const unsigned offset = map->start_dw[i];
      if (offset >= map->per_vertex_start_dw &&
          offset < map->per_vertex_start_dw + map->per_vertex_pitch_dw) {
         fprintf(fp, "  %4d: %s\n", offset,
                 gl_varying_slot_name_for_stage((gl_varying_slot)i,
                                                MESA_SHADER_MESH));
      }
   }

   fprintf(fp, "\n");
}

static void
brw_nir_lower_mue_outputs(nir_shader *nir, const struct brw_mue_map *map)
{
   nir_foreach_shader_out_variable(var, nir) {
      int location = var->data.location;
      assert(location >= 0);
      assert(map->start_dw[location] != -1);
      var->data.driver_location = map->start_dw[location];
   }

   NIR_PASS(_, nir, nir_lower_io, nir_var_shader_out,
            type_size_scalar_dwords, nir_lower_io_lower_64bit_to_32);
}

static void
brw_nir_initialize_mue(nir_shader *nir,
                       const struct brw_mue_map *map,
                       unsigned dispatch_width)
{
   assert(map->per_primitive_header_size_dw > 0);

   nir_builder b;
   nir_function_impl *entrypoint = nir_shader_get_entrypoint(nir);
   nir_builder_init(&b, entrypoint);
   b.cursor = nir_before_block(nir_start_block(entrypoint));

   nir_ssa_def *dw_off = nir_imm_int(&b, 0);
   nir_ssa_def *zerovec = nir_imm_vec4(&b, 0, 0, 0, 0);

   /* TODO(mesh): can we write in bigger batches, generating fewer SENDs? */

   assert(!nir->info.workgroup_size_variable);
   const unsigned workgroup_size = nir->info.workgroup_size[0] *
                                   nir->info.workgroup_size[1] *
                                   nir->info.workgroup_size[2];

   /* Invocations from a single workgroup will cooperate in zeroing MUE. */

   /* How many prims each invocation needs to cover without checking its index? */
   unsigned prims_per_inv = map->max_primitives / workgroup_size;

   /* Zero first 4 dwords of MUE Primitive Header:
    * Reserved, RTAIndex, ViewportIndex, CullPrimitiveMask.
    */

   nir_ssa_def *local_invocation_index = nir_load_local_invocation_index(&b);

   /* Zero primitive headers distanced by workgroup_size, starting from
    * invocation index.
    */
   for (unsigned prim_in_inv = 0; prim_in_inv < prims_per_inv; ++prim_in_inv) {
      nir_ssa_def *prim = nir_iadd_imm(&b, local_invocation_index,
                                           prim_in_inv * workgroup_size);

      nir_store_per_primitive_output(&b, zerovec, prim, dw_off,
                                     .base = (int)map->per_primitive_start_dw,
                                     .write_mask = WRITEMASK_XYZW,
                                     .component = 0,
                                     .src_type = nir_type_uint32);
   }

   /* How many prims are left? */
   unsigned remaining = map->max_primitives % workgroup_size;

   if (remaining) {
      /* Zero "remaining" primitive headers starting from the last one covered
       * by the loop above + workgroup_size.
       */
      nir_ssa_def *cmp = nir_ilt(&b, local_invocation_index,
                                     nir_imm_int(&b, remaining));
      nir_if *if_stmt = nir_push_if(&b, cmp);
      {
         nir_ssa_def *prim = nir_iadd_imm(&b, local_invocation_index,
                                               prims_per_inv * workgroup_size);

         nir_store_per_primitive_output(&b, zerovec, prim, dw_off,
                                        .base = (int)map->per_primitive_start_dw,
                                        .write_mask = WRITEMASK_XYZW,
                                        .component = 0,
                                        .src_type = nir_type_uint32);
      }
      nir_pop_if(&b, if_stmt);
   }

   /* If there's more than one subgroup, then we need to wait for all of them
    * to finish initialization before we can proceed. Otherwise some subgroups
    * may start filling MUE before other finished initializing.
    */
   if (workgroup_size > dispatch_width) {
      nir_scoped_barrier(&b, NIR_SCOPE_WORKGROUP, NIR_SCOPE_WORKGROUP,
                         NIR_MEMORY_ACQ_REL, nir_var_shader_out);
   }

   if (remaining) {
      nir_metadata_preserve(entrypoint, nir_metadata_none);
   } else {
      nir_metadata_preserve(entrypoint, nir_metadata_block_index |
                                        nir_metadata_dominance);
   }
}

static void
brw_nir_adjust_offset(nir_builder *b, nir_intrinsic_instr *intrin, uint32_t pitch)
{
   nir_src *index_src = nir_get_io_arrayed_index_src(intrin);
   nir_src *offset_src = nir_get_io_offset_src(intrin);

   assert(index_src->is_ssa);
   b->cursor = nir_before_instr(&intrin->instr);
   nir_ssa_def *offset =
      nir_iadd(b,
               offset_src->ssa,
               nir_imul_imm(b, index_src->ssa, pitch));
   nir_instr_rewrite_src(&intrin->instr, offset_src, nir_src_for_ssa(offset));
}

static bool
brw_nir_adjust_offset_for_arrayed_indices_instr(nir_builder *b, nir_instr *instr, void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   const struct brw_mue_map *map = (const struct brw_mue_map *) data;

   /* Remap per_vertex and per_primitive offsets using the extra source and
    * the pitch.
    */
   switch (intrin->intrinsic) {
   case nir_intrinsic_load_per_vertex_output:
   case nir_intrinsic_store_per_vertex_output:
      brw_nir_adjust_offset(b, intrin, map->per_vertex_pitch_dw);

      return true;

   case nir_intrinsic_load_per_primitive_output:
   case nir_intrinsic_store_per_primitive_output: {
      struct nir_io_semantics sem = nir_intrinsic_io_semantics(intrin);
      uint32_t pitch;
      if (sem.location == VARYING_SLOT_PRIMITIVE_INDICES)
         pitch = num_mesh_vertices_per_primitive(b->shader->info.mesh.primitive_type);
      else
         pitch = map->per_primitive_pitch_dw;

      brw_nir_adjust_offset(b, intrin, pitch);

      return true;
   }

   default:
      return false;
   }
}

static bool
brw_nir_adjust_offset_for_arrayed_indices(nir_shader *nir, const struct brw_mue_map *map)
{
   return nir_shader_instructions_pass(nir,
                                       brw_nir_adjust_offset_for_arrayed_indices_instr,
                                       nir_metadata_block_index |
                                       nir_metadata_dominance,
                                       (void *)map);
}

const unsigned *
brw_compile_mesh(const struct brw_compiler *compiler,
                 void *mem_ctx,
                 struct brw_compile_mesh_params *params)
{
   struct nir_shader *nir = params->nir;
   const struct brw_mesh_prog_key *key = params->key;
   struct brw_mesh_prog_data *prog_data = params->prog_data;
   const bool debug_enabled = INTEL_DEBUG(DEBUG_MESH);

   prog_data->base.base.stage = MESA_SHADER_MESH;
   prog_data->base.base.total_shared = nir->info.shared_size;
   prog_data->base.base.total_scratch = 0;

   prog_data->base.local_size[0] = nir->info.workgroup_size[0];
   prog_data->base.local_size[1] = nir->info.workgroup_size[1];
   prog_data->base.local_size[2] = nir->info.workgroup_size[2];

   prog_data->clip_distance_mask = (1 << nir->info.clip_distance_array_size) - 1;
   prog_data->cull_distance_mask =
         ((1 << nir->info.cull_distance_array_size) - 1) <<
          nir->info.clip_distance_array_size;
   prog_data->primitive_type = nir->info.mesh.primitive_type;

   /* TODO(mesh): Use other index formats (that are more compact) for optimization. */
   prog_data->index_format = BRW_INDEX_FORMAT_U32;

   prog_data->uses_drawid =
      BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_DRAW_ID);

   brw_nir_lower_tue_inputs(nir, params->tue_map);

   brw_compute_mue_map(nir, &prog_data->map);
   brw_nir_lower_mue_outputs(nir, &prog_data->map);

   brw_simd_selection_state simd_state{
      .mem_ctx = mem_ctx,
      .devinfo = compiler->devinfo,
      .prog_data = &prog_data->base,
      .required_width = brw_required_dispatch_width(&nir->info),
   };

   std::unique_ptr<fs_visitor> v[3];

   for (int simd = 0; simd < 3; simd++) {
      if (!brw_simd_should_compile(simd_state, simd))
         continue;

      const unsigned dispatch_width = 8 << simd;

      nir_shader *shader = nir_shader_clone(mem_ctx, nir);

      /*
       * When Primitive Header is enabled, we may not generates writes to all
       * fields, so let's initialize everything.
       */
      if (prog_data->map.per_primitive_header_size_dw > 0)
         NIR_PASS_V(shader, brw_nir_initialize_mue, &prog_data->map, dispatch_width);

      brw_nir_apply_key(shader, compiler, &key->base, dispatch_width, true /* is_scalar */);

      NIR_PASS(_, shader, brw_nir_adjust_offset_for_arrayed_indices, &prog_data->map);
      /* Load uniforms can do a better job for constants, so fold before it. */
      NIR_PASS(_, shader, nir_opt_constant_folding);
      NIR_PASS(_, shader, brw_nir_lower_load_uniforms);

      NIR_PASS(_, shader, brw_nir_lower_simd, dispatch_width);

      brw_postprocess_nir(shader, compiler, true /* is_scalar */, debug_enabled,
                          key->base.robust_buffer_access);

      brw_nir_adjust_payload(shader, compiler);

      v[simd] = std::make_unique<fs_visitor>(compiler, params->log_data, mem_ctx, &key->base,
                                             &prog_data->base.base, shader, dispatch_width,
                                             debug_enabled);

      if (prog_data->base.prog_mask) {
         unsigned first = ffs(prog_data->base.prog_mask) - 1;
         v[simd]->import_uniforms(v[first].get());
      }

      const bool allow_spilling = !brw_simd_any_compiled(simd_state);
      if (v[simd]->run_mesh(allow_spilling))
         brw_simd_mark_compiled(simd_state, simd, v[simd]->spilled_any_registers);
      else
         simd_state.error[simd] = ralloc_strdup(mem_ctx, v[simd]->fail_msg);
   }

   int selected_simd = brw_simd_select(simd_state);
   if (selected_simd < 0) {
      params->error_str = ralloc_asprintf(mem_ctx, "Can't compile shader: %s, %s and %s.\n",
                                          simd_state.error[0], simd_state.error[1],
                                          simd_state.error[2]);;
      return NULL;
   }

   fs_visitor *selected = v[selected_simd].get();
   prog_data->base.prog_mask = 1 << selected_simd;

   if (unlikely(debug_enabled)) {
      if (params->tue_map) {
         fprintf(stderr, "Mesh Input ");
         brw_print_tue_map(stderr, params->tue_map);
      }
      fprintf(stderr, "Mesh Output ");
      brw_print_mue_map(stderr, &prog_data->map);
   }

   fs_generator g(compiler, params->log_data, mem_ctx,
                  &prog_data->base.base, false, MESA_SHADER_MESH);
   if (unlikely(debug_enabled)) {
      g.enable_debug(ralloc_asprintf(mem_ctx,
                                     "%s mesh shader %s",
                                     nir->info.label ? nir->info.label
                                                     : "unnamed",
                                     nir->info.name));
   }

   g.generate_code(selected->cfg, selected->dispatch_width, selected->shader_stats,
                   selected->performance_analysis.require(), params->stats);
   g.add_const_data(nir->constant_data, nir->constant_data_size);
   return g.get_assembly();
}

static unsigned
component_from_intrinsic(nir_intrinsic_instr *instr)
{
   if (nir_intrinsic_has_component(instr))
      return nir_intrinsic_component(instr);
   else
      return 0;
}

static void
adjust_handle_and_offset(const fs_builder &bld,
                         fs_reg &urb_handle,
                         unsigned &urb_global_offset)
{
   /* Make sure that URB global offset is below 2048 (2^11), because
    * that's the maximum possible value encoded in Message Descriptor.
    */
   unsigned adjustment = (urb_global_offset >> 11) << 11;

   if (adjustment) {
      fs_builder ubld8 = bld.group(8, 0).exec_all();
      ubld8.ADD(urb_handle, urb_handle, brw_imm_ud(adjustment));
      urb_global_offset -= adjustment;
   }
}

static void
emit_urb_direct_vec4_write(const fs_builder &bld,
                           unsigned urb_global_offset,
                           const fs_reg &src,
                           fs_reg urb_handle,
                           unsigned src_comp_offset,
                           unsigned dst_comp_offset,
                           unsigned comps,
                           unsigned mask)
{
   for (unsigned q = 0; q < bld.dispatch_width() / 8; q++) {
      fs_builder bld8 = bld.group(8, q);

      fs_reg payload_srcs[4];
      unsigned length = 0;

      for (unsigned i = 0; i < dst_comp_offset; i++)
         payload_srcs[length++] = reg_undef;

      for (unsigned c = 0; c < comps; c++)
         payload_srcs[length++] = quarter(offset(src, bld, c + src_comp_offset), q);

      fs_reg srcs[URB_LOGICAL_NUM_SRCS];
      srcs[URB_LOGICAL_SRC_HANDLE] = urb_handle;
      srcs[URB_LOGICAL_SRC_CHANNEL_MASK] = brw_imm_ud(mask << 16);
      srcs[URB_LOGICAL_SRC_DATA] = fs_reg(VGRF, bld.shader->alloc.allocate(length),
                                          BRW_REGISTER_TYPE_F);
      bld8.LOAD_PAYLOAD(srcs[URB_LOGICAL_SRC_DATA], payload_srcs, length, 0);

      fs_inst *inst = bld8.emit(SHADER_OPCODE_URB_WRITE_LOGICAL,
                                reg_undef, srcs, ARRAY_SIZE(srcs));
      inst->mlen = 2 + length;
      inst->offset = urb_global_offset;
      assert(inst->offset < 2048);
   }
}

static void
emit_urb_direct_writes(const fs_builder &bld, nir_intrinsic_instr *instr,
                       const fs_reg &src, fs_reg urb_handle)
{
   assert(nir_src_bit_size(instr->src[0]) == 32);

   nir_src *offset_nir_src = nir_get_io_offset_src(instr);
   assert(nir_src_is_const(*offset_nir_src));

   const unsigned comps = nir_src_num_components(instr->src[0]);
   assert(comps <= 4);

   const unsigned mask = nir_intrinsic_write_mask(instr);
   const unsigned offset_in_dwords = nir_intrinsic_base(instr) +
                                     nir_src_as_uint(*offset_nir_src) +
                                     component_from_intrinsic(instr);

   /* URB writes are vec4 aligned but the intrinsic offsets are in dwords.
    * With a max of 4 components, an intrinsic can require up to two writes.
    *
    * First URB write will be shifted by comp_shift.  If there are other
    * components left, then dispatch a second write.  In addition to that,
    * take mask into account to decide whether each write will be actually
    * needed.
    */
   const unsigned comp_shift   = offset_in_dwords % 4;
   const unsigned first_comps  = MIN2(comps, 4 - comp_shift);
   const unsigned second_comps = comps - first_comps;
   const unsigned first_mask   = (mask << comp_shift) & 0xF;
   const unsigned second_mask  = (mask >> (4 - comp_shift)) & 0xF;

   unsigned urb_global_offset = offset_in_dwords / 4;
   adjust_handle_and_offset(bld, urb_handle, urb_global_offset);

   if (first_mask > 0)
      emit_urb_direct_vec4_write(bld, urb_global_offset, src, urb_handle, 0, comp_shift, first_comps, first_mask);

   if (second_mask > 0) {
      urb_global_offset++;
      adjust_handle_and_offset(bld, urb_handle, urb_global_offset);

      emit_urb_direct_vec4_write(bld, urb_global_offset, src, urb_handle, first_comps, 0, second_comps, second_mask);
   }
}

static void
emit_urb_indirect_writes(const fs_builder &bld, nir_intrinsic_instr *instr,
                         const fs_reg &src, const fs_reg &offset_src,
                         fs_reg urb_handle)
{
   assert(nir_src_bit_size(instr->src[0]) == 32);

   const unsigned comps = nir_src_num_components(instr->src[0]);
   assert(comps <= 4);

   const unsigned base_in_dwords = nir_intrinsic_base(instr) +
                                   component_from_intrinsic(instr);

   /* Use URB write message that allow different offsets per-slot.  The offset
    * is in units of vec4s (128 bits), so we use a write for each component,
    * replicating it in the sources and applying the appropriate mask based on
    * the dword offset.
    */

   for (unsigned c = 0; c < comps; c++) {
      if (((1 << c) & nir_intrinsic_write_mask(instr)) == 0)
         continue;

      fs_reg src_comp = offset(src, bld, c);

      for (unsigned q = 0; q < bld.dispatch_width() / 8; q++) {
         fs_builder bld8 = bld.group(8, q);

         fs_reg off = bld8.vgrf(BRW_REGISTER_TYPE_UD, 1);
         bld8.MOV(off, quarter(offset_src, q));
         bld8.ADD(off, off, brw_imm_ud(c + base_in_dwords));

         fs_reg mask = bld8.vgrf(BRW_REGISTER_TYPE_UD, 1);
         bld8.AND(mask, off, brw_imm_ud(0x3));

         fs_reg one = bld8.vgrf(BRW_REGISTER_TYPE_UD, 1);
         bld8.MOV(one, brw_imm_ud(1));
         bld8.SHL(mask, one, mask);
         bld8.SHL(mask, mask, brw_imm_ud(16));

         bld8.SHR(off, off, brw_imm_ud(2));

         fs_reg payload_srcs[4];
         unsigned length = 0;

         for (unsigned j = 0; j < 4; j++)
            payload_srcs[length++] = quarter(src_comp, q);

         fs_reg srcs[URB_LOGICAL_NUM_SRCS];
         srcs[URB_LOGICAL_SRC_HANDLE] = urb_handle;
         srcs[URB_LOGICAL_SRC_PER_SLOT_OFFSETS] = off;
         srcs[URB_LOGICAL_SRC_CHANNEL_MASK] = mask;
         srcs[URB_LOGICAL_SRC_DATA] = fs_reg(VGRF, bld.shader->alloc.allocate(length),
                                             BRW_REGISTER_TYPE_F);
         bld8.LOAD_PAYLOAD(srcs[URB_LOGICAL_SRC_DATA], payload_srcs, length, 0);

         fs_inst *inst = bld8.emit(SHADER_OPCODE_URB_WRITE_LOGICAL,
                                   reg_undef, srcs, ARRAY_SIZE(srcs));
         inst->mlen = 3 + length;
         inst->offset = 0;
      }
   }
}

static void
emit_urb_direct_reads(const fs_builder &bld, nir_intrinsic_instr *instr,
                      const fs_reg &dest, fs_reg urb_handle)
{
   assert(nir_dest_bit_size(instr->dest) == 32);

   unsigned comps = nir_dest_num_components(instr->dest);
   if (comps == 0)
      return;

   nir_src *offset_nir_src = nir_get_io_offset_src(instr);
   assert(nir_src_is_const(*offset_nir_src));

   const unsigned offset_in_dwords = nir_intrinsic_base(instr) +
                                     nir_src_as_uint(*offset_nir_src) +
                                     component_from_intrinsic(instr);

   unsigned urb_global_offset = offset_in_dwords / 4;
   adjust_handle_and_offset(bld, urb_handle, urb_global_offset);

   const unsigned comp_offset = offset_in_dwords % 4;
   const unsigned num_regs = comp_offset + comps;

   fs_builder ubld8 = bld.group(8, 0).exec_all();
   fs_reg data = ubld8.vgrf(BRW_REGISTER_TYPE_UD, num_regs);
   fs_reg srcs[URB_LOGICAL_NUM_SRCS];
   srcs[URB_LOGICAL_SRC_HANDLE] = urb_handle;

   fs_inst *inst = ubld8.emit(SHADER_OPCODE_URB_READ_LOGICAL, data,
                              srcs, ARRAY_SIZE(srcs));
   inst->mlen = 1;
   inst->offset = urb_global_offset;
   assert(inst->offset < 2048);
   inst->size_written = num_regs * REG_SIZE;

   for (unsigned c = 0; c < comps; c++) {
      fs_reg dest_comp = offset(dest, bld, c);
      fs_reg data_comp = horiz_stride(offset(data, ubld8, comp_offset + c), 0);
      bld.MOV(retype(dest_comp, BRW_REGISTER_TYPE_UD), data_comp);
   }
}

static void
emit_urb_indirect_reads(const fs_builder &bld, nir_intrinsic_instr *instr,
                        const fs_reg &dest, const fs_reg &offset_src, fs_reg urb_handle)
{
   assert(nir_dest_bit_size(instr->dest) == 32);

   unsigned comps = nir_dest_num_components(instr->dest);
   if (comps == 0)
      return;

   fs_reg seq_ud;
   {
      fs_builder ubld8 = bld.group(8, 0).exec_all();
      seq_ud = ubld8.vgrf(BRW_REGISTER_TYPE_UD, 1);
      fs_reg seq_uw = ubld8.vgrf(BRW_REGISTER_TYPE_UW, 1);
      ubld8.MOV(seq_uw, fs_reg(brw_imm_v(0x76543210)));
      ubld8.MOV(seq_ud, seq_uw);
      ubld8.SHL(seq_ud, seq_ud, brw_imm_ud(2));
   }

   const unsigned base_in_dwords = nir_intrinsic_base(instr) +
                                   component_from_intrinsic(instr);

   for (unsigned c = 0; c < comps; c++) {
      for (unsigned q = 0; q < bld.dispatch_width() / 8; q++) {
         fs_builder bld8 = bld.group(8, q);

         fs_reg off = bld8.vgrf(BRW_REGISTER_TYPE_UD, 1);
         bld8.MOV(off, quarter(offset_src, q));
         bld8.ADD(off, off, brw_imm_ud(base_in_dwords + c));

         STATIC_ASSERT(IS_POT(REG_SIZE) && REG_SIZE > 1);

         fs_reg comp = bld8.vgrf(BRW_REGISTER_TYPE_UD, 1);
         bld8.AND(comp, off, brw_imm_ud(0x3));
         bld8.SHL(comp, comp, brw_imm_ud(ffs(REG_SIZE) - 1));
         bld8.ADD(comp, comp, seq_ud);

         bld8.SHR(off, off, brw_imm_ud(2));

         fs_reg srcs[URB_LOGICAL_NUM_SRCS];
         srcs[URB_LOGICAL_SRC_HANDLE] = urb_handle;
         srcs[URB_LOGICAL_SRC_PER_SLOT_OFFSETS] = off;

         fs_reg data = bld8.vgrf(BRW_REGISTER_TYPE_UD, 4);

         fs_inst *inst = bld8.emit(SHADER_OPCODE_URB_READ_LOGICAL,
                                   data, srcs, ARRAY_SIZE(srcs));
         inst->mlen = 2;
         inst->offset = 0;
         inst->size_written = 4 * REG_SIZE;

         fs_reg dest_comp = offset(dest, bld, c);
         bld8.emit(SHADER_OPCODE_MOV_INDIRECT,
                   retype(quarter(dest_comp, q), BRW_REGISTER_TYPE_UD),
                   data,
                   comp,
                   brw_imm_ud(4 * REG_SIZE));
      }
   }
}

void
fs_visitor::emit_task_mesh_store(const fs_builder &bld, nir_intrinsic_instr *instr,
                                 const fs_reg &urb_handle)
{
   fs_reg src = get_nir_src(instr->src[0]);
   nir_src *offset_nir_src = nir_get_io_offset_src(instr);

   fs_builder ubld8 = bld.group(8, 0).exec_all();
   fs_reg h = ubld8.vgrf(BRW_REGISTER_TYPE_UD, 1);
   ubld8.MOV(h, urb_handle);
   ubld8.AND(h, h, brw_imm_ud(0xFFFF));

   /* TODO(mesh): for per_vertex and per_primitive, if we could keep around
    * the non-array-index offset, we could use to decide if we can perform
    * either one or (at most) two writes instead one per component.
    */

   if (nir_src_is_const(*offset_nir_src))
      emit_urb_direct_writes(bld, instr, src, h);
   else
      emit_urb_indirect_writes(bld, instr, src, get_nir_src(*offset_nir_src), h);
}

void
fs_visitor::emit_task_mesh_load(const fs_builder &bld, nir_intrinsic_instr *instr,
                                const fs_reg &urb_handle, bool mask)
{
   fs_reg dest = get_nir_dest(instr->dest);
   nir_src *offset_nir_src = nir_get_io_offset_src(instr);

   fs_builder ubld8 = bld.group(8, 0).exec_all();
   fs_reg h = ubld8.vgrf(BRW_REGISTER_TYPE_UD, 1);
   ubld8.MOV(h, urb_handle);
   if (mask)
      ubld8.AND(h, h, brw_imm_ud(0xFFFF));

   /* TODO(mesh): for per_vertex and per_primitive, if we could keep around
    * the non-array-index offset, we could use to decide if we can perform
    * a single large aligned read instead one per component.
    */

   if (nir_src_is_const(*offset_nir_src))
      emit_urb_direct_reads(bld, instr, dest, h);
   else
      emit_urb_indirect_reads(bld, instr, dest, get_nir_src(*offset_nir_src), h);
}

void
fs_visitor::nir_emit_task_intrinsic(const fs_builder &bld,
                                    nir_intrinsic_instr *instr)
{
   assert(stage == MESA_SHADER_TASK);
   const task_mesh_thread_payload &payload = task_mesh_payload();

   switch (instr->intrinsic) {
   case nir_intrinsic_store_output:
   case nir_intrinsic_store_task_payload:
      emit_task_mesh_store(bld, instr, payload.urb_output);
      break;

   case nir_intrinsic_load_output:
   case nir_intrinsic_load_task_payload:
      emit_task_mesh_load(bld, instr, payload.urb_output, true);
      break;

   default:
      nir_emit_task_mesh_intrinsic(bld, instr);
      break;
   }
}

void
fs_visitor::nir_emit_mesh_intrinsic(const fs_builder &bld,
                                    nir_intrinsic_instr *instr)
{
   assert(stage == MESA_SHADER_MESH);
   const task_mesh_thread_payload &payload = task_mesh_payload();

   switch (instr->intrinsic) {
   case nir_intrinsic_store_per_primitive_output:
   case nir_intrinsic_store_per_vertex_output:
   case nir_intrinsic_store_output:
      emit_task_mesh_store(bld, instr, payload.urb_output);
      break;

   case nir_intrinsic_load_per_vertex_output:
   case nir_intrinsic_load_per_primitive_output:
   case nir_intrinsic_load_output:
      emit_task_mesh_load(bld, instr, payload.urb_output, true);
      break;

   case nir_intrinsic_load_task_payload:
      emit_task_mesh_load(bld, instr, payload.task_urb_input, false);
      break;

   default:
      nir_emit_task_mesh_intrinsic(bld, instr);
      break;
   }
}

void
fs_visitor::nir_emit_task_mesh_intrinsic(const fs_builder &bld,
                                         nir_intrinsic_instr *instr)
{
   assert(stage == MESA_SHADER_MESH || stage == MESA_SHADER_TASK);
   const task_mesh_thread_payload &payload = task_mesh_payload();

   fs_reg dest;
   if (nir_intrinsic_infos[instr->intrinsic].has_dest)
      dest = get_nir_dest(instr->dest);

   switch (instr->intrinsic) {
   case nir_intrinsic_load_mesh_inline_data_intel: {
      fs_reg data = offset(payload.inline_parameter, 1, nir_intrinsic_align_offset(instr));
      bld.MOV(dest, retype(data, dest.type));
      break;
   }

   case nir_intrinsic_load_draw_id:
      dest = retype(dest, BRW_REGISTER_TYPE_UD);
      bld.MOV(dest, payload.extended_parameter_0);
      break;

   case nir_intrinsic_load_local_invocation_index:
   case nir_intrinsic_load_local_invocation_id:
      dest = retype(dest, BRW_REGISTER_TYPE_UD);
      bld.MOV(dest, payload.local_index);
      /* Task/Mesh only use one dimension. */
      if (instr->intrinsic == nir_intrinsic_load_local_invocation_id) {
         bld.MOV(offset(dest, bld, 1), brw_imm_uw(0));
         bld.MOV(offset(dest, bld, 2), brw_imm_uw(0));
      }
      break;

   case nir_intrinsic_load_num_workgroups:
      assert(!nir->info.mesh.nv);
      dest = retype(dest, BRW_REGISTER_TYPE_UD);
      bld.SHR(offset(dest, bld, 0), retype(brw_vec1_grf(0, 6), dest.type), brw_imm_ud(16));
      bld.AND(offset(dest, bld, 1), retype(brw_vec1_grf(0, 4), dest.type), brw_imm_ud(0xffff));
      bld.SHR(offset(dest, bld, 2), retype(brw_vec1_grf(0, 4), dest.type), brw_imm_ud(16));
      break;

   case nir_intrinsic_load_workgroup_index:
      dest = retype(dest, BRW_REGISTER_TYPE_UD);
      bld.MOV(dest, retype(brw_vec1_grf(0, 1), BRW_REGISTER_TYPE_UD));
      break;

   default:
      nir_emit_cs_intrinsic(bld, instr);
      break;
   }
}
