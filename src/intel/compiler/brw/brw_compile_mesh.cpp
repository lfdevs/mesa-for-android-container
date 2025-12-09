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

#include <list>
#include "brw_compiler.h"
#include "brw_shader.h"
#include "brw_builder.h"
#include "brw_generator.h"
#include "brw_nir.h"
#include "brw_private.h"
#include "compiler/nir/nir_builder.h"
#include "dev/intel_debug.h"

#include <memory>

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
brw_nir_lower_launch_mesh_workgroups_instr(nir_builder *b,
                                           nir_intrinsic_instr *intrin,
                                           void *data)
{
   if (intrin->intrinsic != nir_intrinsic_launch_mesh_workgroups)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *local_invocation_index = nir_load_local_invocation_index(b);

   /* Make sure that the mesh workgroup size is taken from the first invocation
    * (nir_intrinsic_launch_mesh_workgroups requirement)
    */
   nir_def *cmp = nir_ieq_imm(b, local_invocation_index, 0);
   nir_if *if_stmt = nir_push_if(b, cmp);
   {
      /* TUE header contains 4 words:
       *
       * - Word 0 for Task Count.
       *
       * - Words 1-3 used for "Dispatch Dimensions" feature, to allow mapping a
       *   3D dispatch into the 1D dispatch supported by HW.
       */
      nir_def *x = nir_channel(b, intrin->src[0].ssa, 0);
      nir_def *y = nir_channel(b, intrin->src[0].ssa, 1);
      nir_def *z = nir_channel(b, intrin->src[0].ssa, 2);
      nir_def *task_count = nir_imul(b, x, nir_imul(b, y, z));
      nir_def *tue_header = nir_vec4(b, task_count, x, y, z);
      nir_store_task_payload(b, tue_header, nir_imm_int(b, 0));
   }
   nir_pop_if(b, if_stmt);

   nir_instr_remove(&intrin->instr);

   return true;
}

static bool
brw_nir_lower_launch_mesh_workgroups(nir_shader *nir)
{
   return nir_shader_intrinsics_pass(nir,
                                       brw_nir_lower_launch_mesh_workgroups_instr,
                                       nir_metadata_none,
                                       NULL);
}

/* From bspec: "It is suggested that SW reserve the 16 bytes following the
 * TUE Header, and therefore start the SW-defined data structure at 32B
 * alignment.  This allows the TUE Header to always be written as 32 bytes
 * with 32B alignment, the most optimal write performance case."
 */
#define BRW_PER_TASK_DATA_START_DW 8

static void
brw_nir_lower_tue_outputs(nir_shader *nir, brw_tue_map *map)
{
   memset(map, 0, sizeof(*map));

   NIR_PASS(_, nir, nir_lower_io, nir_var_shader_out,
            type_size_scalar_dwords, nir_lower_io_lower_64bit_to_32);

   /* Lowering to explicit types will start offsets from task_payload_size, so
    * set it to start after the header.
    */
   nir->info.task_payload_size = BRW_PER_TASK_DATA_START_DW * 4;
   NIR_PASS(_, nir, nir_lower_vars_to_explicit_types,
            nir_var_mem_task_payload, shared_type_info);
   NIR_PASS(_, nir, nir_lower_explicit_io,
            nir_var_mem_task_payload, nir_address_format_32bit_offset);

   map->size_dw = align(DIV_ROUND_UP(nir->info.task_payload_size, 4), 8);
}

static void
brw_print_tue_map(FILE *fp, const struct brw_tue_map *map)
{
   fprintf(fp, "TUE (%d dwords)\n\n", map->size_dw);
}

static bool
brw_nir_adjust_task_payload_offsets_instr(struct nir_builder *b,
                                          nir_intrinsic_instr *intrin,
                                          void *data)
{
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

      nir_def *offset = nir_ishr_imm(b, offset_src->ssa, 2);
      nir_src_rewrite(offset_src, offset);

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
   return nir_shader_intrinsics_pass(nir,
                                       brw_nir_adjust_task_payload_offsets_instr,
                                       nir_metadata_control_flow,
                                       NULL);
}

void
brw_nir_adjust_payload(nir_shader *shader)
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
brw_nir_align_launch_mesh_workgroups_instr(nir_builder *b,
                                           nir_intrinsic_instr *intrin,
                                           void *data)
{
   if (intrin->intrinsic != nir_intrinsic_launch_mesh_workgroups)
      return false;

   /* nir_lower_task_shader uses "range" as task payload size. */
   unsigned range = nir_intrinsic_range(intrin);
   /* This will avoid special case in nir_lower_task_shader dealing with
    * not vec4-aligned payload when payload_in_shared workaround is enabled.
    */
   nir_intrinsic_set_range(intrin, align(range, 16));

   return true;
}

static bool
brw_nir_align_launch_mesh_workgroups(nir_shader *nir)
{
   return nir_shader_intrinsics_pass(nir,
                                       brw_nir_align_launch_mesh_workgroups_instr,
                                       nir_metadata_control_flow,
                                       NULL);
}

static bool
lower_set_vtx_and_prim_to_temp_write(nir_builder *b,
                                     nir_intrinsic_instr *intrin,
                                     void *data)
{
   if (intrin->intrinsic != nir_intrinsic_set_vertex_and_primitive_count)
      return false;

   /* Detect some cases of invalid primitive count. They might lead to URB
    * memory corruption, where workgroups overwrite each other output memory.
    */
   if (nir_src_is_const(intrin->src[1]) &&
       nir_src_as_uint(intrin->src[1]) > b->shader->info.mesh.max_primitives_out)
      UNREACHABLE("number of primitives bigger than max specified");

   b->cursor = nir_instr_remove(&intrin->instr);

   nir_variable *temporary_primitive_count = (nir_variable *)data;
   nir_store_var(b, temporary_primitive_count, intrin->src[1].ssa, 0x1);

   return true;
}

static bool
brw_nir_lower_mesh_primitive_count(nir_shader *nir)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);

   nir_variable *temporary_primitive_count =
      nir_local_variable_create(impl,
                                glsl_uint_type(),
                                "__temp_primitive_count");

   nir_shader_intrinsics_pass(nir,
                              lower_set_vtx_and_prim_to_temp_write,
                              nir_metadata_control_flow,
                              temporary_primitive_count);

   nir_builder _b = nir_builder_at(nir_before_impl(impl)), *b = &_b;

   nir_store_var(b, temporary_primitive_count, nir_imm_int(b, 0), 0x1);

   b->cursor = nir_after_impl(impl);

   /* Have a single lane write the primitive count */
   nir_def *local_invocation_index = nir_load_local_invocation_index(b);
   nir_push_if(b, nir_ieq_imm(b, local_invocation_index, 0));
   {
      nir_variable *final_primitive_count =
         nir_create_variable_with_location(nir, nir_var_shader_out,
                                           VARYING_SLOT_PRIMITIVE_COUNT,
                                           glsl_uint_type());
      nir_variable_set_name(nir, final_primitive_count, "gl_PrimitiveCountNV");
      final_primitive_count->data.interpolation = INTERP_MODE_NONE;

      nir_store_var(b, final_primitive_count,
                    nir_load_var(b, temporary_primitive_count), 0x1);
   }
   nir_pop_if(b, NULL);

   nir_progress(true, impl, nir_metadata_none);

   nir->info.outputs_written |= VARYING_BIT_PRIMITIVE_COUNT;

   return true;
}

static void
brw_emit_urb_fence(brw_shader &s)
{
   const brw_builder bld1 = brw_builder(&s).uniform();
   brw_reg dst = bld1.vgrf(BRW_TYPE_UD);
   brw_send_inst *fence = bld1.emit(SHADER_OPCODE_MEMORY_FENCE, dst,
                                    brw_vec8_grf(0, 0),
                                    brw_imm_ud(true))->as_send();
   fence->size_written = REG_SIZE * reg_unit(s.devinfo);
   fence->sfid = BRW_SFID_URB;
   /* The logical thing here would likely be a THREADGROUP fence but that's
    * still failing some tests like in dEQP-VK.mesh_shader.ext.query.*
    *
    * Gfx12.5 has a comment about this on BSpec 53533 :
    *
    *    "If fence scope is Local or Threadgroup, HW ignores the flush type
    *     and operates as if it was set to None (no flush)"
    *
    * Software workaround from HSD-22014129519 indicates that a GPU fence
    * resolves the issue.
    */
   fence->desc = lsc_fence_msg_desc(s.devinfo, LSC_FENCE_GPU,
                                    LSC_FLUSH_TYPE_NONE, true);

   bld1.emit(FS_OPCODE_SCHEDULING_FENCE, bld1.null_reg_ud(), &dst, 1);
}

static bool
run_task_mesh(brw_shader &s, bool allow_spilling)
{
   assert(s.stage == MESA_SHADER_TASK ||
          s.stage == MESA_SHADER_MESH);

   s.payload_ = new brw_task_mesh_thread_payload(s);

   brw_from_nir(&s);

   if (s.failed)
      return false;

   brw_emit_urb_fence(s);

   s.emit_cs_terminate();

   brw_calculate_cfg(s);

   brw_optimize(s);

   s.assign_curb_setup();

   brw_lower_3src_null_dest(s);
   brw_workaround_emit_dummy_mov_instruction(s);

   brw_allocate_registers(s, allow_spilling);

   brw_workaround_source_arf_before_eot(s);

   return !s.failed;
}

const unsigned *
brw_compile_task(const struct brw_compiler *compiler,
                 struct brw_compile_task_params *params)
{
   const struct intel_device_info *devinfo = compiler->devinfo;
   struct nir_shader *nir = params->base.nir;
   const struct brw_task_prog_key *key = params->key;
   struct brw_task_prog_data *prog_data = params->prog_data;
   const bool debug_enabled = brw_should_print_shader(nir, DEBUG_TASK, params->base.source_hash);

   brw_debug_archive_nir(params->base.archiver, nir, 0, "first");

   brw_nir_lower_tue_outputs(nir, &prog_data->map);

   NIR_PASS(_, nir, brw_nir_align_launch_mesh_workgroups);

   nir_lower_task_shader_options lower_ts_opt = {
      .payload_to_shared_for_atomics = true,
      .payload_to_shared_for_small_types = true,
      /* The actual payload data starts after the TUE header and padding,
       * so skip those when copying.
       */
      .payload_offset_in_bytes = BRW_PER_TASK_DATA_START_DW * 4,
   };
   NIR_PASS(_, nir, nir_lower_task_shader, lower_ts_opt);

   NIR_PASS(_, nir, brw_nir_lower_launch_mesh_workgroups);

   brw_prog_data_init(&prog_data->base.base, &params->base);

   prog_data->base.local_size[0] = nir->info.workgroup_size[0];
   prog_data->base.local_size[1] = nir->info.workgroup_size[1];
   prog_data->base.local_size[2] = nir->info.workgroup_size[2];

   prog_data->uses_drawid =
      BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_DRAW_ID);

   prog_data->base.uses_inline_data = brw_nir_uses_inline_data(nir) ||
                                      key->base.uses_inline_push_addr;

   brw_postprocess_nir_opts(nir, compiler, key->base.robust_flags);

   brw_simd_selection_state simd_state{
      .devinfo = compiler->devinfo,
      .prog_data = &prog_data->base,
      .required_width = brw_required_dispatch_width(&nir->info),
   };

   brw_debug_archive_nir(params->base.archiver, nir, 0, "before-simd");

   unsigned pressure[SIMD_COUNT];
   brw_nir_quick_pressure_estimate(nir, devinfo, pressure);

   for (unsigned i = 0; i < SIMD_COUNT; i++) {
      simd_state.beyond_threshold[i] =
         pressure[i] > compiler->register_pressure_threshold;
   }

   std::unique_ptr<brw_shader> v[3];

   for (unsigned i = 0; i < 3; i++) {
      const unsigned simd = devinfo->ver >= 30 ? 2 - i : i;

      if (!brw_simd_should_compile(simd_state, simd))
         continue;

      const unsigned dispatch_width = 8 << simd;

      nir_shader *shader = nir_shader_clone(params->base.mem_ctx, nir);
      brw_nir_apply_key(shader, compiler, &key->base, dispatch_width);

      brw_debug_archive_nir(params->base.archiver, shader, dispatch_width, "first");

      NIR_PASS(_, shader, brw_nir_lower_simd, dispatch_width);

      brw_nir_optimize(shader, devinfo);
      brw_postprocess_nir_out_of_ssa(shader, dispatch_width,
                                     params->base.archiver, debug_enabled);

      const brw_shader_params shader_params = {
         .compiler                = compiler,
         .mem_ctx                 = params->base.mem_ctx,
         .nir                     = shader,
         .key                     = &key->base,
         .prog_data               = &prog_data->base.base,
         .dispatch_width          = dispatch_width,
         .needs_register_pressure = params->base.stats != NULL,
         .log_data                = params->base.log_data,
         .debug_enabled           = debug_enabled,
         .archiver                = params->base.archiver,
      };
      v[simd] = std::make_unique<brw_shader>(&shader_params);

      const bool allow_spilling = simd == 0 ||
         (!simd_state.compiled[simd - 1] && !brw_simd_should_compile(simd_state, simd - 1));
      if (run_task_mesh(*v[simd], allow_spilling)) {
         brw_simd_mark_compiled(simd_state, simd, v[simd]->spilled_any_registers);

         if (devinfo->ver >= 30 && !v[simd]->spilled_any_registers)
            break;
      } else {
         simd_state.error[simd] = ralloc_strdup(params->base.mem_ctx, v[simd]->fail_msg);
      }
   }

   int selected_simd = brw_simd_select(simd_state);
   if (selected_simd < 0) {
      params->base.error_str =
         ralloc_asprintf(params->base.mem_ctx,
                         "Can't compile shader: "
                         "SIMD8 '%s', SIMD16 '%s' and SIMD32 '%s'.\n",
                         simd_state.error[0], simd_state.error[1],
                         simd_state.error[2]);
      return NULL;
   }

   brw_shader &selected = *v[selected_simd];
   prog_data->base.prog_mask = 1 << selected_simd;
   prog_data->base.base.grf_used = MAX2(prog_data->base.base.grf_used,
                                        selected.grf_used);

   if (unlikely(debug_enabled)) {
      fprintf(stderr, "Task Output ");
      brw_print_tue_map(stderr, &prog_data->map);
   }

   brw_generator g(compiler, &params->base, &prog_data->base.base,
                  MESA_SHADER_TASK);
   if (unlikely(debug_enabled)) {
      g.enable_debug(ralloc_asprintf(params->base.mem_ctx,
                                     "%s task shader %s",
                                     nir->info.label ? nir->info.label
                                                     : "unnamed",
                                     nir->info.name));
   }

   g.generate_code(selected, params->base.stats);
   g.add_const_data(nir->constant_data, nir->constant_data_size);
   return g.get_assembly();
}

static void
brw_nir_lower_tue_inputs(nir_shader *nir, const brw_tue_map *map)
{
   /* See brw_nir_lower_tue_outputs. If a task payload is read by this shader,
    * task_payload_size will be used to start offsets, and that's always
    * header + padding.
    * We can't always use map, as it may not be present if task and mesh
    * shaders are not compiled together. This is possible with shader objects.
    */
   nir->info.task_payload_size = BRW_PER_TASK_DATA_START_DW * 4;

   bool progress = false;

   NIR_PASS(progress, nir, nir_lower_vars_to_explicit_types,
            nir_var_mem_task_payload, shared_type_info);

   if (progress) {
      /* The types for Task Output and Mesh Input should match, so their sizes
       * should also match.
       */
      assert(!map || map->size_dw == align(DIV_ROUND_UP(nir->info.task_payload_size, 4), 8));
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

/* Attribute types. Flat attributes have to be a separate class because
 * flat and interpolated attributes can't share the same vec4 slot
 * (see 3DSTATE_SBE.ConstantInterpolationEnable).
 */
enum {
   PRIM, /* per primitive */
   VERT, /* per vertex interpolated */
   VERT_FLAT, /* per vertex flat */
};

struct attr_desc {
   int location;
   const struct glsl_type *type;
   unsigned dwords;
   unsigned slots;
};

static void
brw_compute_mue_map(const struct brw_compiler *compiler,
                    nir_shader *nir, struct brw_mue_map *map,
                    enum brw_mesh_index_format index_format,
                    enum intel_vue_layout vue_layout,
                    int *wa_18019110168_mapping)
{
   memset(map, 0, sizeof(*map));

   map->max_primitives = nir->info.mesh.max_primitives_out;
   map->max_vertices = nir->info.mesh.max_vertices_out;

   /* NumPrimitives */
   map->size += 4;

   /* PrimX indices */
   const unsigned vertices_per_primitive =
      mesa_vertices_per_prim(nir->info.mesh.primitive_type);

   switch (index_format) {
   case BRW_INDEX_FORMAT_U32:
      map->per_primitive_indices_stride = 4 * vertices_per_primitive;
      break;
   case BRW_INDEX_FORMAT_U888X:
      map->per_primitive_indices_stride = 4;
      break;
   default:
      UNREACHABLE("invalid index format");
   }

   map->size += map->per_primitive_indices_stride * map->max_primitives;

   /* Per primitive blocks */
   map->size = align(map->size, 32);
   map->per_primitive_offset = map->size;

   const uint64_t count_indices_bits =
      VARYING_BIT_PRIMITIVE_COUNT |
      VARYING_BIT_PRIMITIVE_INDICES;
   const uint64_t per_primitive_header_bits =
      VARYING_BIT_PRIMITIVE_SHADING_RATE |
      VARYING_BIT_LAYER |
      VARYING_BIT_VIEWPORT |
      VARYING_BIT_CULL_PRIMITIVE;

   /* Do we need a header? */
   map->has_per_primitive_header =
      (nir->info.outputs_written &
       nir->info.per_primitive_outputs &
       per_primitive_header_bits) != 0;

   uint32_t first_per_prim_offset;
   brw_compute_per_primitive_map(map->per_primitive_offsets,
                                 &map->per_primitive_stride,
                                 &first_per_prim_offset,
                                 map->has_per_primitive_header ? 32 : 0,
                                 nir, nir_var_shader_out,
                                 nir->info.outputs_written &
                                 nir->info.per_primitive_outputs,
                                 vue_layout != INTEL_VUE_LAYOUT_FIXED);

   map->per_primitive_offsets[VARYING_SLOT_PRIMITIVE_COUNT] = 0;
   map->per_primitive_offsets[VARYING_SLOT_PRIMITIVE_INDICES] = 4;
   if (map->has_per_primitive_header) {
      /* Setup all the fields in the header */
      map->per_primitive_offsets[VARYING_SLOT_PRIMITIVE_SHADING_RATE] = 0;
      map->per_primitive_offsets[VARYING_SLOT_LAYER] = 4;
      map->per_primitive_offsets[VARYING_SLOT_VIEWPORT] = 8;
      map->per_primitive_offsets[VARYING_SLOT_CULL_PRIMITIVE] = 12;
   }

   /* If Wa_18019110168 is active, store the remapping in the
    * per_primitive_offsets array.
    */
   if (wa_18019110168_mapping) {
      map->wa_18019110168_active = true;
      for (uint32_t i = 0; i < ARRAY_SIZE(map->per_primitive_offsets); i++) {
         if (i == VARYING_SLOT_PRIMITIVE_COUNT ||
             i == VARYING_SLOT_PRIMITIVE_INDICES ||
             i == VARYING_SLOT_PRIMITIVE_SHADING_RATE ||
             i == VARYING_SLOT_LAYER ||
             i == VARYING_SLOT_VIEWPORT ||
             i == VARYING_SLOT_CULL_PRIMITIVE)
            continue;
         map->per_primitive_offsets[i] = wa_18019110168_mapping[i];
      }
   }

   map->per_primitive_stride = align(map->per_primitive_stride, 32);

   map->size += map->per_primitive_stride * map->max_primitives;
   assert(map->size % 32 == 0);

   assert((nir->info.outputs_written & VARYING_BIT_PRIMITIVE_ID) == 0 ||
          (nir->info.outputs_written & nir->info.per_primitive_outputs) != 0);

   /* Per vertex blocks:
    *
    * For some selected bit that can appear either as per-primitive or
    * per-vertex inputs to the fragment shader, we need to add them to the
    * per-vertex block as well so that the layouts match. Even though they're
    * not written.
    */
   const uint64_t per_primitive_outputs =
      nir->info.outputs_written & nir->info.per_primitive_outputs;
   const uint64_t per_vertex_outputs =
      (nir->info.outputs_written &
       ~(per_primitive_outputs | count_indices_bits | per_primitive_header_bits));

   map->per_vertex_offset = map->size;
   brw_compute_vue_map(compiler->devinfo,
                       &map->vue_map, per_vertex_outputs,
                       vue_layout, 1 /* pos_slots, TODO: multiview */);
   map->per_vertex_stride = align(map->vue_map.num_slots * 16, 32);
   map->size += map->per_vertex_stride * map->max_vertices;
   assert(map->size % 32 == 0);
}

static void
brw_print_mue_map(FILE *fp, const struct brw_mue_map *map, struct nir_shader *nir)
{
   fprintf(fp, "MUE map (%d bytes, %d primitives, %d vertices):\n",
           map->size, map->max_primitives, map->max_vertices);
   fprintf(fp, "   indices_stride:   %d\n", map->per_primitive_indices_stride);
   fprintf(fp, "   primitive_header: %d\n", map->has_per_primitive_header);
   fprintf(fp, "   primitive_offset: %d\n", map->per_primitive_offset);
   fprintf(fp, "   primitive_stride: %d\n", map->per_primitive_stride);
   fprintf(fp, "   vertex_offset:    %d\n", map->per_vertex_offset);
   fprintf(fp, "   vertex_stride:    %d\n", map->per_vertex_stride);

   fprintf(fp, "   primitive offsets:\n");
   fprintf(fp, "      %s: %d\n",
           gl_varying_slot_name_for_stage(VARYING_SLOT_PRIMITIVE_COUNT,
                                          MESA_SHADER_MESH),
           map->per_primitive_offsets[VARYING_SLOT_PRIMITIVE_COUNT]);
   fprintf(fp, "      %s: %d\n",
           gl_varying_slot_name_for_stage(VARYING_SLOT_PRIMITIVE_INDICES,
                                          MESA_SHADER_MESH),
           map->per_primitive_offsets[VARYING_SLOT_PRIMITIVE_INDICES]);
   for (uint32_t i = 0; i < VARYING_SLOT_MAX; i++) {
      if (map->per_primitive_offsets[i] < 0 ||
          i == VARYING_SLOT_PRIMITIVE_COUNT ||
          i == VARYING_SLOT_PRIMITIVE_INDICES)
         continue;
      fprintf(fp, "      %s: %d (relative %d)\n",
              gl_varying_slot_name_for_stage((gl_varying_slot)i,
                                             MESA_SHADER_MESH),
              map->per_primitive_offset + map->per_primitive_offsets[i],
              map->per_primitive_offsets[i]);
   }
   brw_print_vue_map(fp, &map->vue_map, MESA_SHADER_MESH);
}

static bool
remap_io_to_dwords(nir_builder *b, nir_intrinsic_instr *intrin, void *data)
{
   if (intrin->intrinsic != nir_intrinsic_load_per_vertex_output &&
       intrin->intrinsic != nir_intrinsic_load_per_primitive_output &&
       intrin->intrinsic != nir_intrinsic_store_per_vertex_output &&
       intrin->intrinsic != nir_intrinsic_store_per_primitive_output)
      return false;

   nir_io_semantics io_sem = nir_intrinsic_io_semantics(intrin);
   if (io_sem.location == VARYING_SLOT_PRIMITIVE_INDICES ||
       io_sem.location == VARYING_SLOT_PRIMITIVE_COUNT)
      return false;

   nir_intrinsic_set_base(intrin, nir_intrinsic_base(intrin) * 4);
   if (nir_intrinsic_has_range(intrin))
      nir_intrinsic_set_range(intrin, nir_intrinsic_range(intrin) * 4);

   b->cursor = nir_before_instr(&intrin->instr);

   nir_src *offset = nir_get_io_offset_src(intrin);
   assert(offset != NULL);

   nir_src_rewrite(offset, nir_ishl_imm(b, offset->ssa, 2));

   io_sem.no_validate = true;
   nir_intrinsic_set_io_semantics(intrin, io_sem);

   return true;
}

static void
brw_nir_lower_mue_outputs(nir_shader *nir, const struct brw_mue_map *map)
{
   nir_foreach_shader_out_variable(var, nir) {
      int location = var->data.location;
      assert(location >= 0);

      switch (location) {
      case VARYING_SLOT_PRIMITIVE_COUNT:
      case VARYING_SLOT_PRIMITIVE_INDICES:
         /* Primitive count & indices are not part of the per-primitive block,
          * they have there own spot just before. We saved their offset in the
          * the per-primitive array, we just don't need to add the block
          * offset.
          */
         var->data.driver_location =
            map->per_primitive_offsets[location] / 4;
         break;

      case VARYING_SLOT_PRIMITIVE_SHADING_RATE:
         var->data.driver_location = map->per_primitive_offset / 16;
         var->data.location_frac = 0;
         break;

      case VARYING_SLOT_LAYER:
         var->data.driver_location = map->per_primitive_offset / 16;
         var->data.location_frac = 1;
         break;

      case VARYING_SLOT_VIEWPORT:
         var->data.driver_location = map->per_primitive_offset / 16;
         var->data.location_frac = 2;
         break;

      case VARYING_SLOT_CULL_PRIMITIVE:
         var->data.driver_location = map->per_primitive_offset / 16;
         var->data.location_frac = 3;
         break;

      case VARYING_SLOT_PSIZ:
         var->data.driver_location = map->per_vertex_offset / 16;
         var->data.location_frac = 3;
         break;

      default:
         if (nir->info.per_primitive_outputs & BITFIELD64_BIT(location))  {
            assert(map->per_primitive_offsets[location] != -1);
            var->data.driver_location =
               (map->per_primitive_offset +
                map->per_primitive_offsets[location]) / 16;
         } else {
            /* Each per vertex location has its own slot/vec4 (16B) of data, use
             * map->vue_map.varying_to_slot[] to get the 16B offset and add the
             * per-vertex block offset.
             */
            assert(map->vue_map.varying_to_slot[location] != -1);
            var->data.driver_location =
               map->per_vertex_offset / 16 +
               map->vue_map.varying_to_slot[location];
         }
         break;
      }
   }

   NIR_PASS(_, nir, nir_lower_io, nir_var_shader_out,
            type_size_vec4,
            nir_lower_io_lower_64bit_to_32);

   /* Everythings works with slots in terms if IO, but our backend deals with
    * dwords. Apply remapping.
    */
   NIR_PASS(_, nir, nir_shader_intrinsics_pass,
            remap_io_to_dwords, nir_metadata_control_flow, NULL);
}

static bool
brw_nir_initialize_mue(nir_shader *nir,
                       const struct brw_mue_map *map,
                       unsigned dispatch_width)
{
   nir_builder b;
   nir_function_impl *entrypoint = nir_shader_get_entrypoint(nir);
   b = nir_builder_at(nir_before_impl(entrypoint));

   nir_def *dw_off = nir_imm_int(&b, 0);
   nir_def *zerovec = nir_imm_vec4(&b, 0, 0, 0, 0);

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

   nir_def *local_invocation_index = nir_load_local_invocation_index(&b);

   /* Zero primitive headers distanced by workgroup_size, starting from
    * invocation index.
    */
   for (unsigned prim_in_inv = 0; prim_in_inv < prims_per_inv; ++prim_in_inv) {
      nir_def *prim = nir_iadd_imm(&b, local_invocation_index,
                                           prim_in_inv * workgroup_size);

      nir_store_per_primitive_output(&b, zerovec, prim, dw_off,
                                     .base = (int)map->per_primitive_offset / 4,
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
      nir_def *cmp = nir_ilt_imm(&b, local_invocation_index, remaining);
      nir_if *if_stmt = nir_push_if(&b, cmp);
      {
         nir_def *prim = nir_iadd_imm(&b, local_invocation_index,
                                               prims_per_inv * workgroup_size);

         nir_store_per_primitive_output(&b, zerovec, prim, dw_off,
                                        .base = (int)map->per_primitive_offset / 4,
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
      nir_barrier(&b, SCOPE_WORKGROUP, SCOPE_WORKGROUP,
                         NIR_MEMORY_ACQ_REL, nir_var_shader_out);
   }

   if (remaining) {
      nir_progress(true, entrypoint, nir_metadata_none);
   } else {
      nir_progress(true, entrypoint, nir_metadata_control_flow);
   }
   return true;
}

static void
brw_nir_adjust_offset(nir_builder *b, nir_intrinsic_instr *intrin, uint32_t pitch)
{
   nir_src *index_src = nir_get_io_arrayed_index_src(intrin);
   nir_src *offset_src = nir_get_io_offset_src(intrin);

   b->cursor = nir_before_instr(&intrin->instr);
   nir_def *offset =
      nir_iadd(b,
               offset_src->ssa,
               nir_imul_imm(b, index_src->ssa, pitch));
   nir_src_rewrite(offset_src, offset);

   nir_io_semantics io_sem = nir_intrinsic_io_semantics(intrin);
   io_sem.no_validate = true;
   nir_intrinsic_set_io_semantics(intrin, io_sem);
}

static bool
brw_nir_adjust_offset_for_arrayed_indices_instr(nir_builder *b,
                                                nir_intrinsic_instr *intrin,
                                                void *data)
{
   const struct brw_mue_map *map = (const struct brw_mue_map *) data;

   /* Remap per_vertex and per_primitive offsets using the extra source and
    * the pitch.
    */
   switch (intrin->intrinsic) {
   case nir_intrinsic_load_per_vertex_output:
   case nir_intrinsic_store_per_vertex_output:
      brw_nir_adjust_offset(b, intrin, map->per_vertex_stride / 4);
      return true;

   case nir_intrinsic_load_per_primitive_output:
   case nir_intrinsic_store_per_primitive_output: {
      struct nir_io_semantics sem = nir_intrinsic_io_semantics(intrin);
      uint32_t pitch;
      if (sem.location == VARYING_SLOT_PRIMITIVE_INDICES)
         pitch = map->per_primitive_indices_stride;
      else
         pitch = map->per_primitive_stride;

      brw_nir_adjust_offset(b, intrin, pitch / 4);
      return true;
   }

   default:
      return false;
   }
}

static bool
brw_nir_adjust_offset_for_arrayed_indices(nir_shader *nir, const struct brw_mue_map *map)
{
   return nir_shader_intrinsics_pass(nir,
                                       brw_nir_adjust_offset_for_arrayed_indices_instr,
                                       nir_metadata_control_flow,
                                       (void *)map);
}

struct index_packing_state {
   unsigned vertices_per_primitive;
   nir_variable *original_prim_indices;
   nir_variable *packed_prim_indices;
};

static bool
brw_can_pack_primitive_indices(nir_shader *nir, struct index_packing_state *state)
{
   /* can single index fit into one byte of U888X format? */
   if (nir->info.mesh.max_vertices_out > 255)
      return false;

   state->vertices_per_primitive =
         mesa_vertices_per_prim(nir->info.mesh.primitive_type);
   /* packing point indices doesn't help */
   if (state->vertices_per_primitive == 1)
      return false;

   state->original_prim_indices =
      nir_find_variable_with_location(nir,
                                      nir_var_shader_out,
                                      VARYING_SLOT_PRIMITIVE_INDICES);
   /* no indices = no changes to the shader, but it's still worth it,
    * because less URB space will be used
    */
   if (!state->original_prim_indices)
      return true;

   ASSERTED const struct glsl_type *type = state->original_prim_indices->type;
   assert(glsl_type_is_array(type));
   assert(glsl_type_is_vector(glsl_without_array(type)));
   assert(glsl_without_array(type)->vector_elements == state->vertices_per_primitive);

   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

            if (intrin->intrinsic != nir_intrinsic_store_deref) {
               /* any unknown deref operation on primitive indices -> don't pack */
               unsigned num_srcs = nir_intrinsic_infos[intrin->intrinsic].num_srcs;
               for (unsigned i = 0; i < num_srcs; i++) {
                  nir_deref_instr *deref = nir_src_as_deref(intrin->src[i]);
                  if (!deref)
                     continue;
                  nir_variable *var = nir_deref_instr_get_variable(deref);

                  if (var == state->original_prim_indices)
                     return false;
               }

               continue;
            }

            nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
            if (!deref)
               continue;

            nir_variable *var = nir_deref_instr_get_variable(deref);
            if (var != state->original_prim_indices)
               continue;

            if (deref->deref_type != nir_deref_type_array)
               return false; /* unknown chain of derefs */

            nir_deref_instr *var_deref = nir_src_as_deref(deref->parent);
            if (!var_deref || var_deref->deref_type != nir_deref_type_var)
               return false; /* unknown chain of derefs */

            assert (var_deref->var == state->original_prim_indices);

            unsigned write_mask = nir_intrinsic_write_mask(intrin);

            /* If only some components are written, then we can't easily pack.
             * In theory we could, by loading current dword value, bitmasking
             * one byte and storing back the whole dword, but it would be slow
             * and could actually decrease performance. TODO: reevaluate this
             * once there will be something hitting this.
             */
            if (write_mask != BITFIELD_MASK(state->vertices_per_primitive))
               return false;
         }
      }
   }

   return true;
}

static bool
brw_pack_primitive_indices_instr(nir_builder *b, nir_intrinsic_instr *intrin,
                                 void *data)
{
   if (intrin->intrinsic != nir_intrinsic_store_deref)
      return false;

   nir_deref_instr *array_deref = nir_src_as_deref(intrin->src[0]);
   if (!array_deref || array_deref->deref_type != nir_deref_type_array)
      return false;

   nir_deref_instr *var_deref = nir_src_as_deref(array_deref->parent);
   if (!var_deref || var_deref->deref_type != nir_deref_type_var)
      return false;

   struct index_packing_state *state =
         (struct index_packing_state *)data;

   nir_variable *var = var_deref->var;

   if (var != state->original_prim_indices)
      return false;

   unsigned vertices_per_primitive = state->vertices_per_primitive;

   b->cursor = nir_before_instr(&intrin->instr);

   nir_deref_instr *new_var_deref =
         nir_build_deref_var(b, state->packed_prim_indices);
   nir_deref_instr *new_array_deref =
         nir_build_deref_array(b, new_var_deref, array_deref->arr.index.ssa);

   nir_src *data_src = &intrin->src[1];
   nir_def *data_def =
         data_src->ssa;

   nir_def *new_data =
         nir_ior(b, nir_ishl_imm(b, nir_channel(b, data_def, 0), 0),
                    nir_ishl_imm(b, nir_channel(b, data_def, 1), 8));

   if (vertices_per_primitive >= 3) {
      new_data =
            nir_ior(b, new_data,
                       nir_ishl_imm(b, nir_channel(b, data_def, 2), 16));
   }

   nir_build_store_deref(b, &new_array_deref->def, new_data);

   nir_instr_remove(&intrin->instr);

   return true;
}

static bool
brw_pack_primitive_indices(nir_shader *nir, void *data)
{
   struct index_packing_state *state = (struct index_packing_state *)data;

   const struct glsl_type *new_type =
         glsl_array_type(glsl_uint_type(),
                         nir->info.mesh.max_primitives_out,
                         0);

   state->packed_prim_indices =
         nir_variable_create(nir, nir_var_shader_out,
                             new_type, "gl_PrimitiveIndicesPacked");
   state->packed_prim_indices->data.location = VARYING_SLOT_PRIMITIVE_INDICES;
   state->packed_prim_indices->data.interpolation = INTERP_MODE_NONE;
   state->packed_prim_indices->data.per_primitive = 1;

   return nir_shader_intrinsics_pass(nir, brw_pack_primitive_indices_instr,
                                       nir_metadata_control_flow,
                                       data);
}

static bool
brw_mesh_autostrip_enable(const struct brw_compiler *compiler, struct nir_shader *nir,
                          struct brw_mue_map *map)
{
   /* Auto-striping can be enabled when shader either doesn't write to
    * RTA Index and VP Index or writes the same values for all primitives.
    * Since determining whether shader writes the same value across the whole
    * workgroup (not just subgroup!) is tricky, we do the simplest possible
    * thing - say yes only when shader writes const values and they all match.
    *
    * TODO: improve this
    */

   if (compiler->devinfo->ver < 20)
      return false;

   const uint64_t outputs_written = nir->info.outputs_written;

   /* Wa_16020916187
    * We've allocated slots for layer/viewport in brw_compute_mue_map() if this
    * workaround is needed and will let brw_nir_initialize_mue() initialize
    * those to 0. The workaround also requires disabling autostrip.
    */
   if (intel_needs_workaround(compiler->devinfo, 16020916187) &&
       (VARYING_BIT_PRIMITIVE_SHADING_RATE & outputs_written))
       return false;

   /* Values not written */
   if ((outputs_written & (VARYING_BIT_VIEWPORT |
                           VARYING_BIT_LAYER)) == 0)
      return true;

   nir_def *vp = NULL;
   nir_def *layer = NULL;

   nir_foreach_function(function, nir) {
      if (!function->impl)
         continue;

      nir_foreach_block(block, function->impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            if (intrin->intrinsic != nir_intrinsic_store_per_primitive_output)
               continue;

            struct nir_io_semantics io = nir_intrinsic_io_semantics(intrin);
            bool is_vp = io.location == VARYING_SLOT_VIEWPORT;
            bool is_layer = io.location == VARYING_SLOT_LAYER;
            if (!is_vp && !is_layer)
               continue;

            nir_src *src = &intrin->src[0];

            if (!nir_src_is_const(*src))
               return false;

            nir_def **cmp;
            if (is_vp)
               cmp = &vp;
            else
               cmp = &layer;

            if (*cmp == NULL)
               *cmp = src->ssa;
            else if (*cmp != src->ssa)
               return false;
         }
      }
   }

   return true;
}

const unsigned *
brw_compile_mesh(const struct brw_compiler *compiler,
                 struct brw_compile_mesh_params *params)
{
   const struct intel_device_info *devinfo = compiler->devinfo;
   struct nir_shader *nir = params->base.nir;
   const struct brw_mesh_prog_key *key = params->key;
   struct brw_mesh_prog_data *prog_data = params->prog_data;
   const bool debug_enabled = brw_should_print_shader(nir, DEBUG_MESH, params->base.source_hash);

   brw_debug_archive_nir(params->base.archiver, nir, 0, "first");

   brw_prog_data_init(&prog_data->base.base, &params->base);

   prog_data->base.local_size[0] = nir->info.workgroup_size[0];
   prog_data->base.local_size[1] = nir->info.workgroup_size[1];
   prog_data->base.local_size[2] = nir->info.workgroup_size[2];

   prog_data->primitive_type = nir->info.mesh.primitive_type;

   /* Apply this workaround before trying to pack indices because this can
    * increase the number of vertices and therefore change the decision about
    * packing.
    */
   const bool apply_wa_18019110168 =
      brw_nir_mesh_shader_needs_wa_18019110168(devinfo, nir);
   int wa_18019110168_mapping[VARYING_SLOT_MAX];
   memset(wa_18019110168_mapping, -1, sizeof(wa_18019110168_mapping));
   if (apply_wa_18019110168) {
      brw_nir_mesh_convert_attrs_prim_to_vert(nir, params,
                                              wa_18019110168_mapping);
   }

   struct index_packing_state index_packing_state = {};
   if (brw_can_pack_primitive_indices(nir, &index_packing_state)) {
      if (index_packing_state.original_prim_indices)
         NIR_PASS(_, nir, brw_pack_primitive_indices, &index_packing_state);
      prog_data->index_format = BRW_INDEX_FORMAT_U888X;
   } else {
      prog_data->index_format = BRW_INDEX_FORMAT_U32;
   }

   prog_data->uses_drawid =
      BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_DRAW_ID);

   brw_nir_lower_tue_inputs(nir, params->tue_map);

   NIR_PASS(_, nir, brw_nir_lower_mesh_primitive_count);
   NIR_PASS(_, nir, nir_opt_dce);
   NIR_PASS(_, nir, nir_remove_dead_variables, nir_var_shader_out, NULL);

   brw_compute_mue_map(compiler, nir, &prog_data->map,
                       prog_data->index_format,
                       key->base.vue_layout,
                       apply_wa_18019110168 ? wa_18019110168_mapping : NULL);
   brw_nir_lower_mue_outputs(nir, &prog_data->map);

   prog_data->autostrip_enable = brw_mesh_autostrip_enable(compiler, nir, &prog_data->map);

   prog_data->base.uses_inline_data = brw_nir_uses_inline_data(nir) ||
                                      key->base.uses_inline_push_addr;

   brw_postprocess_nir_opts(nir, compiler, key->base.robust_flags);

   brw_simd_selection_state simd_state{
      .devinfo = compiler->devinfo,
      .prog_data = &prog_data->base,
      .required_width = brw_required_dispatch_width(&nir->info),
   };

   std::unique_ptr<brw_shader> v[3];

   brw_debug_archive_nir(params->base.archiver, nir, 0, "before-simd");

   for (unsigned i = 0; i < 3; i++) {
      const unsigned simd = devinfo->ver >= 30 ? 2 - i : i;

      if (!brw_simd_should_compile(simd_state, simd))
         continue;

      const unsigned dispatch_width = 8 << simd;

      nir_shader *shader = nir_shader_clone(params->base.mem_ctx, nir);

      brw_debug_archive_nir(params->base.archiver, shader, dispatch_width, "first");

      /*
       * When Primitive Header is enabled, we may not generates writes to all
       * fields, so let's initialize everything.
       */
      if (prog_data->map.has_per_primitive_header)
         NIR_PASS(_, shader, brw_nir_initialize_mue, &prog_data->map, dispatch_width);

      brw_nir_apply_key(shader, compiler, &key->base, dispatch_width);

      NIR_PASS(_, shader, brw_nir_adjust_offset_for_arrayed_indices, &prog_data->map);
      /* Load uniforms can do a better job for constants, so fold before it. */
      NIR_PASS(_, shader, nir_opt_constant_folding);

      NIR_PASS(_, shader, brw_nir_lower_simd, dispatch_width);

      brw_nir_optimize(shader, devinfo);
      brw_postprocess_nir_out_of_ssa(shader, dispatch_width,
                                     params->base.archiver, debug_enabled);

      const brw_shader_params shader_params = {
         .compiler                = compiler,
         .mem_ctx                 = params->base.mem_ctx,
         .nir                     = shader,
         .key                     = &key->base,
         .prog_data               = &prog_data->base.base,
         .dispatch_width          = dispatch_width,
         .needs_register_pressure = params->base.stats != NULL,
         .log_data                = params->base.log_data,
         .debug_enabled           = debug_enabled,
         .archiver                = params->base.archiver,
      };
      v[simd] = std::make_unique<brw_shader>(&shader_params);

      const bool allow_spilling = simd == 0 ||
         (!simd_state.compiled[simd - 1] && !brw_simd_should_compile(simd_state, simd - 1));
      if (run_task_mesh(*v[simd], allow_spilling)) {
         brw_simd_mark_compiled(simd_state, simd, v[simd]->spilled_any_registers);

         if (devinfo->ver >= 30 && !v[simd]->spilled_any_registers)
            break;
      } else {
         simd_state.error[simd] = ralloc_strdup(params->base.mem_ctx, v[simd]->fail_msg);
      }
   }

   int selected_simd = brw_simd_select(simd_state);
   if (selected_simd < 0) {
      params->base.error_str =
         ralloc_asprintf(params->base.mem_ctx,
                         "Can't compile shader: "
                         "SIMD8 '%s', SIMD16 '%s' and SIMD32 '%s'.\n",
                         simd_state.error[0], simd_state.error[1],
                         simd_state.error[2]);
      return NULL;
   }

   brw_shader &selected = *v[selected_simd];
   prog_data->base.prog_mask = 1 << selected_simd;
   prog_data->base.base.grf_used = MAX2(prog_data->base.base.grf_used,
                                        selected.grf_used);

   if (unlikely(debug_enabled)) {
      if (params->tue_map) {
         fprintf(stderr, "Mesh Input ");
         brw_print_tue_map(stderr, params->tue_map);
      }
      fprintf(stderr, "Mesh Output ");
      brw_print_mue_map(stderr, &prog_data->map, nir);
   }

   brw_generator g(compiler, &params->base, &prog_data->base.base,
                  MESA_SHADER_MESH);
   if (unlikely(debug_enabled)) {
      g.enable_debug(ralloc_asprintf(params->base.mem_ctx,
                                     "%s mesh shader %s",
                                     nir->info.label ? nir->info.label
                                                     : "unnamed",
                                     nir->info.name));
   }

   g.generate_code(selected, params->base.stats);
   if (prog_data->map.wa_18019110168_active) {
      int8_t remap_table[VARYING_SLOT_TESS_MAX];
      memset(remap_table, -1, sizeof(remap_table));
      for (uint32_t i = 0; i < ARRAY_SIZE(wa_18019110168_mapping); i++) {
         if (wa_18019110168_mapping[i] != -1)
            remap_table[i] = prog_data->map.vue_map.varying_to_slot[wa_18019110168_mapping[i]];
      }
      uint8_t *const_data =
         (uint8_t *) rzalloc_size(params->base.mem_ctx,
                                  nir->constant_data_size + sizeof(remap_table));
      memcpy(const_data, nir->constant_data, nir->constant_data_size);
      memcpy(const_data + nir->constant_data_size, remap_table, sizeof(remap_table));
      g.add_const_data(const_data, nir->constant_data_size + sizeof(remap_table));
      prog_data->wa_18019110168_mapping_offset =
         prog_data->base.base.const_data_offset + nir->constant_data_size;
   } else {
      g.add_const_data(nir->constant_data, nir->constant_data_size);
   }

   return g.get_assembly();
}
