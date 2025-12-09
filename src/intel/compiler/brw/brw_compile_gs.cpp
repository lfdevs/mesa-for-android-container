/*
 * Copyright © 2013 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_eu.h"
#include "brw_shader.h"
#include "brw_builder.h"
#include "brw_generator.h"
#include "intel_prim.h"
#include "brw_nir.h"
#include "brw_private.h"
#include "dev/intel_debug.h"

static const GLuint gl_prim_to_hw_prim[MESA_PRIM_TRIANGLE_STRIP_ADJACENCY+1] = {
   [MESA_PRIM_POINTS] =_3DPRIM_POINTLIST,
   [MESA_PRIM_LINES] = _3DPRIM_LINELIST,
   [MESA_PRIM_LINE_LOOP] = _3DPRIM_LINELOOP,
   [MESA_PRIM_LINE_STRIP] = _3DPRIM_LINESTRIP,
   [MESA_PRIM_TRIANGLES] = _3DPRIM_TRILIST,
   [MESA_PRIM_TRIANGLE_STRIP] = _3DPRIM_TRISTRIP,
   [MESA_PRIM_TRIANGLE_FAN] = _3DPRIM_TRIFAN,
   [MESA_PRIM_QUADS] = _3DPRIM_QUADLIST,
   [MESA_PRIM_QUAD_STRIP] = _3DPRIM_QUADSTRIP,
   [MESA_PRIM_POLYGON] = _3DPRIM_POLYGON,
   [MESA_PRIM_LINES_ADJACENCY] = _3DPRIM_LINELIST_ADJ,
   [MESA_PRIM_LINE_STRIP_ADJACENCY] = _3DPRIM_LINESTRIP_ADJ,
   [MESA_PRIM_TRIANGLES_ADJACENCY] = _3DPRIM_TRILIST_ADJ,
   [MESA_PRIM_TRIANGLE_STRIP_ADJACENCY] = _3DPRIM_TRISTRIP_ADJ,
};

static void
brw_emit_gs_thread_end(brw_shader &s)
{
   assert(s.stage == MESA_SHADER_GEOMETRY);

   struct brw_gs_prog_data *gs_prog_data = brw_gs_prog_data(s.prog_data);

   if (s.gs.control_data_header_size_bits > 0) {
      s.emit_gs_control_data_bits(s.final_gs_vertex_count);
   }

   const brw_builder abld = brw_builder(&s).annotate("thread end");
   brw_urb_inst *urb;

   if (gs_prog_data->static_vertex_count != -1) {
      /* Try and tag the last URB write with EOT instead of emitting a whole
       * separate write just to finish the thread.
       */
      if (s.mark_last_urb_write_with_eot())
         return;

      brw_reg srcs[URB_LOGICAL_NUM_SRCS];
      srcs[URB_LOGICAL_SRC_HANDLE] = s.gs_payload().urb_handles;
      srcs[URB_LOGICAL_SRC_DATA] = brw_imm_ud(0);
      srcs[URB_LOGICAL_SRC_CHANNEL_MASK] = brw_imm_ud(0);
      urb = abld.URB_WRITE(srcs, ARRAY_SIZE(srcs));
      urb->components = 1;
   } else {
      brw_reg srcs[URB_LOGICAL_NUM_SRCS];
      srcs[URB_LOGICAL_SRC_HANDLE] = s.gs_payload().urb_handles;
      srcs[URB_LOGICAL_SRC_DATA] = s.final_gs_vertex_count;
      urb = abld.URB_WRITE(srcs, ARRAY_SIZE(srcs));
      urb->components = 1;
   }
   urb->eot = true;
   urb->offset = 0;
}

static void
brw_assign_gs_urb_setup(brw_shader &s)
{
   assert(s.stage == MESA_SHADER_GEOMETRY);

   struct brw_vue_prog_data *vue_prog_data = brw_vue_prog_data(s.prog_data);

   s.first_non_payload_grf +=
      8 * vue_prog_data->urb_read_length * s.nir->info.gs.vertices_in;

   foreach_block_and_inst(block, brw_inst, inst, s.cfg) {
      /* Rewrite all ATTR file references to GRFs. */
      s.convert_attr_sources_to_hw_regs(inst);
   }
}

static bool
run_gs(brw_shader &s)
{
   assert(s.stage == MESA_SHADER_GEOMETRY);

   s.payload_ = new brw_gs_thread_payload(s);

   const brw_builder bld = brw_builder(&s);

   s.final_gs_vertex_count = bld.vgrf(BRW_TYPE_UD);

   if (s.gs.control_data_header_size_bits > 0) {
      /* Create a VGRF to store accumulated control data bits. */
      s.control_data_bits = bld.vgrf(BRW_TYPE_UD);

      /* If we're outputting more than 32 control data bits, then EmitVertex()
       * will set control_data_bits to 0 after emitting the first vertex.
       * Otherwise, we need to initialize it to 0 here.
       */
      if (s.gs.control_data_header_size_bits <= 32) {
         const brw_builder abld = bld.annotate("initialize control data bits");
         abld.MOV(s.control_data_bits, brw_imm_ud(0u));
      }
   }

   brw_from_nir(&s);

   brw_emit_gs_thread_end(s);

   if (s.failed)
      return false;

   brw_calculate_cfg(s);

   brw_optimize(s);

   s.assign_curb_setup();
   brw_assign_gs_urb_setup(s);

   brw_lower_3src_null_dest(s);
   brw_workaround_emit_dummy_mov_instruction(s);

   brw_allocate_registers(s, true /* allow_spilling */);

   brw_workaround_source_arf_before_eot(s);

   return !s.failed;
}

extern "C" const unsigned *
brw_compile_gs(const struct brw_compiler *compiler,
               struct brw_compile_gs_params *params)
{
   nir_shader *nir = params->base.nir;
   const struct brw_gs_prog_key *key = params->key;
   struct brw_gs_prog_data *prog_data = params->prog_data;
   const unsigned dispatch_width = brw_geometry_stage_dispatch_width(compiler->devinfo);

   struct intel_vue_map input_vue_map = {0};

   unsigned control_data_bits_per_vertex = 0;
   unsigned control_data_header_size_bits = 0;

   const bool debug_enabled = brw_should_print_shader(nir, DEBUG_GS, params->base.source_hash);

   brw_debug_archive_nir(params->base.archiver, nir, dispatch_width, "first");

   brw_prog_data_init(&prog_data->base.base, &params->base);

   /* The GLSL linker will have already matched up GS inputs and the outputs
    * of prior stages.  The driver does extend VS outputs in some cases, but
    * only for legacy OpenGL or Gfx4-5 hardware, neither of which offer
    * geometry shader support.  So we can safely ignore that.
    *
    * For SSO pipelines, we use a fixed VUE map layout based on variable
    * locations, so we can rely on rendezvous-by-location making this work.
    */
   GLbitfield64 inputs_read = nir->info.inputs_read;
   brw_compute_vue_map(compiler->devinfo,
                       &input_vue_map, inputs_read,
                       key->base.vue_layout, 1);

   const uint32_t pos_slots =
      (nir->info.per_view_outputs & VARYING_BIT_POS) ?
      MAX2(1, util_bitcount(key->base.view_mask)) : 1;

   brw_compute_vue_map(compiler->devinfo,
                       &prog_data->base.vue_map,
                       nir->info.outputs_written,
                       key->base.vue_layout,
                       pos_slots);

   brw_nir_apply_key(nir, compiler, &key->base, dispatch_width);
   brw_nir_lower_vue_inputs(nir, &input_vue_map);
   brw_nir_lower_vue_outputs(nir);
   brw_postprocess_nir(nir, compiler, dispatch_width,
                       params->base.archiver, debug_enabled,
                       key->base.robust_flags);

   prog_data->include_primitive_id =
      BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_PRIMITIVE_ID);

   prog_data->invocations = nir->info.gs.invocations;

   nir_gs_count_vertices_and_primitives(
      nir, &prog_data->static_vertex_count, nullptr, nullptr, 1u);

   if (nir->info.gs.output_primitive == MESA_PRIM_POINTS) {
      /* When the output type is points, the geometry shader may output data
       * to multiple streams, and EndPrimitive() has no effect.  So we
       * configure the hardware to interpret the control data as stream ID.
       */
      prog_data->control_data_format = GFX7_GS_CONTROL_DATA_FORMAT_GSCTL_SID;

      /* We only have to emit control bits if we are using non-zero streams */
      if (nir->info.gs.active_stream_mask != (1 << 0))
         control_data_bits_per_vertex = 2;
      else
         control_data_bits_per_vertex = 0;
   } else {
      /* When the output type is triangle_strip or line_strip, EndPrimitive()
       * may be used to terminate the current strip and start a new one
       * (similar to primitive restart), and outputting data to multiple
       * streams is not supported.  So we configure the hardware to interpret
       * the control data as EndPrimitive information (a.k.a. "cut bits").
       */
      prog_data->control_data_format = GFX7_GS_CONTROL_DATA_FORMAT_GSCTL_CUT;

      /* We only need to output control data if the shader actually calls
       * EndPrimitive().
       */
      control_data_bits_per_vertex =
         nir->info.gs.uses_end_primitive ? 1 : 0;
   }

   control_data_header_size_bits =
      nir->info.gs.vertices_out * control_data_bits_per_vertex;

   /* 1 HWORD = 32 bytes = 256 bits */
   prog_data->control_data_header_size_hwords =
      align(control_data_header_size_bits, 256) / 256;

   /* Compute the output vertex size.
    *
    * From the Ivy Bridge PRM, Vol2 Part1 7.2.1.1 STATE_GS - Output Vertex
    * Size (p168):
    *
    *     [0,62] indicating [1,63] 16B units
    *
    *     Specifies the size of each vertex stored in the GS output entry
    *     (following any Control Header data) as a number of 128-bit units
    *     (minus one).
    *
    *     Programming Restrictions: The vertex size must be programmed as a
    *     multiple of 32B units with the following exception: Rendering is
    *     disabled (as per SOL stage state) and the vertex size output by the
    *     GS thread is 16B.
    *
    *     If rendering is enabled (as per SOL state) the vertex size must be
    *     programmed as a multiple of 32B units. In other words, the only time
    *     software can program a vertex size with an odd number of 16B units
    *     is when rendering is disabled.
    *
    * Note: B=bytes in the above text.
    *
    * It doesn't seem worth the extra trouble to optimize the case where the
    * vertex size is 16B (especially since this would require special-casing
    * the GEN assembly that writes to the URB).  So we just set the vertex
    * size to a multiple of 32B (2 vec4's) in all cases.
    *
    * The maximum output vertex size is 62*16 = 992 bytes (31 hwords).  We
    * budget that as follows:
    *
    *   512 bytes for varyings (a varying component is 4 bytes and
    *             gl_MaxGeometryOutputComponents = 128)
    *    16 bytes overhead for VARYING_SLOT_PSIZ (each varying slot is 16
    *             bytes)
    *    16 bytes overhead for gl_Position (we allocate it a slot in the VUE
    *             even if it's not used)
    *    32 bytes overhead for gl_ClipDistance (we allocate it 2 VUE slots
    *             whenever clip planes are enabled, even if the shader doesn't
    *             write to gl_ClipDistance)
    *    16 bytes overhead since the VUE size must be a multiple of 32 bytes
    *             (see below)--this causes up to 1 VUE slot to be wasted
    *   400 bytes available for varying packing overhead
    *
    * Worst-case varying packing overhead is 3/4 of a varying slot (12 bytes)
    * per interpolation type, so this is plenty.
    *
    */
   unsigned output_vertex_size_bytes = prog_data->base.vue_map.num_slots * 16;
   assert(output_vertex_size_bytes <= GFX7_MAX_GS_OUTPUT_VERTEX_SIZE_BYTES);
   prog_data->output_vertex_size_hwords =
      align(output_vertex_size_bytes, 32) / 32;

   /* Compute URB entry size.  The maximum allowed URB entry size is 32k.
    * That divides up as follows:
    *
    *     64 bytes for the control data header (cut indices or StreamID bits)
    *   4096 bytes for varyings (a varying component is 4 bytes and
    *              gl_MaxGeometryTotalOutputComponents = 1024)
    *   4096 bytes overhead for VARYING_SLOT_PSIZ (each varying slot is 16
    *              bytes/vertex and gl_MaxGeometryOutputVertices is 256)
    *   4096 bytes overhead for gl_Position (we allocate it a slot in the VUE
    *              even if it's not used)
    *   8192 bytes overhead for gl_ClipDistance (we allocate it 2 VUE slots
    *              whenever clip planes are enabled, even if the shader doesn't
    *              write to gl_ClipDistance)
    *   4096 bytes overhead since the VUE size must be a multiple of 32
    *              bytes (see above)--this causes up to 1 VUE slot to be wasted
    *   8128 bytes available for varying packing overhead
    *
    * Worst-case varying packing overhead is 3/4 of a varying slot per
    * interpolation type, which works out to 3072 bytes, so this would allow
    * us to accommodate 2 interpolation types without any danger of running
    * out of URB space.
    *
    * In practice, the risk of running out of URB space is very small, since
    * the above figures are all worst-case, and most of them scale with the
    * number of output vertices.  So we'll just calculate the amount of space
    * we need, and if it's too large, fail to compile.
    *
    * The above is for gfx7+ where we have a single URB entry that will hold
    * all the output.
    */
   unsigned output_size_bytes =
      prog_data->output_vertex_size_hwords * 32 * nir->info.gs.vertices_out;
   output_size_bytes += 32 * prog_data->control_data_header_size_hwords;

   /* Broadwell stores "Vertex Count" as a full 8 DWord (32 byte) URB output,
    * which comes before the control header.
    */
   output_size_bytes += 32;

   /* Shaders can technically set max_vertices = 0, at which point we
    * may have a URB size of 0 bytes.  Nothing good can come from that,
    * so enforce a minimum size.
    */
   if (output_size_bytes == 0)
      output_size_bytes = 1;

   unsigned max_output_size_bytes = GFX7_MAX_GS_URB_ENTRY_SIZE_BYTES;
   if (output_size_bytes > max_output_size_bytes)
      return NULL;


   /* URB entry sizes are stored as a multiple of 64 bytes in gfx7+. */
   prog_data->base.urb_entry_size = align(output_size_bytes, 64) / 64;

   assert(nir->info.gs.output_primitive < ARRAY_SIZE(gl_prim_to_hw_prim));
   prog_data->output_topology =
      gl_prim_to_hw_prim[nir->info.gs.output_primitive];

   prog_data->vertices_in = nir->info.gs.vertices_in;

   /* GS inputs are read from the VUE 256 bits (2 vec4's) at a time, so we
    * need to program a URB read length of ceiling(num_slots / 2).
    */
   prog_data->base.urb_read_length = (input_vue_map.num_slots + 1) / 2;

   /* Now that prog_data setup is done, we are ready to actually compile the
    * program.
    */
   if (unlikely(debug_enabled)) {
      fprintf(stderr, "GS Input ");
      brw_print_vue_map(stderr, &input_vue_map, MESA_SHADER_GEOMETRY);
      fprintf(stderr, "GS Output ");
      brw_print_vue_map(stderr, &prog_data->base.vue_map, MESA_SHADER_GEOMETRY);
   }

   const brw_shader_params shader_params = {
      .compiler                = compiler,
      .mem_ctx                 = params->base.mem_ctx,
      .nir                     = nir,
      .key                     = &key->base,
      .prog_data               = &prog_data->base.base,
      .dispatch_width          = dispatch_width,
      .needs_register_pressure = params->base.stats != NULL,
      .log_data                = params->base.log_data,
      .debug_enabled           = debug_enabled,
      .archiver                = params->base.archiver,
   };
   brw_shader v(&shader_params);
   v.gs.control_data_bits_per_vertex = control_data_bits_per_vertex;
   v.gs.control_data_header_size_bits = control_data_header_size_bits;
   if (run_gs(v)) {
      prog_data->base.dispatch_mode = INTEL_DISPATCH_MODE_SIMD8;

      assert(v.payload().num_regs % reg_unit(compiler->devinfo) == 0);
      prog_data->base.base.dispatch_grf_start_reg =
         v.payload().num_regs / reg_unit(compiler->devinfo);
      prog_data->base.base.grf_used = v.grf_used;

      brw_generator g(compiler, &params->base,
                     &prog_data->base.base, MESA_SHADER_GEOMETRY);
      if (unlikely(debug_enabled)) {
         const char *label =
            nir->info.label ? nir->info.label : "unnamed";
         char *name = ralloc_asprintf(params->base.mem_ctx,
                                      "%s geometry shader %s",
                                      label, nir->info.name);
         g.enable_debug(name);
      }
      g.generate_code(v, params->base.stats);
      g.add_const_data(nir->constant_data, nir->constant_data_size);
      return g.get_assembly();
   }

   params->base.error_str = ralloc_strdup(params->base.mem_ctx, v.fail_msg);

   return NULL;
}
