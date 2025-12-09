/*
 * Copyright © 2011 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_shader.h"
#include "brw_generator.h"
#include "brw_eu.h"
#include "brw_nir.h"
#include "brw_private.h"
#include "dev/intel_debug.h"

static void
brw_assign_vs_urb_setup(brw_shader &s)
{
   struct brw_vs_prog_data *vs_prog_data = brw_vs_prog_data(s.prog_data);

   assert(s.stage == MESA_SHADER_VERTEX);

   /* Each attribute is 4 regs. */
   s.first_non_payload_grf += 8 * vs_prog_data->base.urb_read_length;

   assert(vs_prog_data->base.urb_read_length <= 15);

   /* Rewrite all ATTR file references to the hw grf that they land in. */
   foreach_block_and_inst(block, brw_inst, inst, s.cfg) {
      s.convert_attr_sources_to_hw_regs(inst);
   }
}

static unsigned
brw_nir_pack_vs_input(nir_shader *nir, struct brw_vs_prog_data *prog_data)
{
   struct vf_attribute {
      unsigned reg_offset;
      uint8_t  component_mask;
      bool     is_64bit:1;
      bool     is_used:1;
   } attributes[MAX_HW_VERT_ATTRIB] = {};

   /* IO lowering is going to break dmat inputs into a location each, so we
    * need to reproduce the 64bit nature of the variable into each slot.
    */
   nir_foreach_shader_in_variable(var, nir) {
      const bool is_64bit = glsl_type_is_64bit(var->type);
      const uint32_t slots = glsl_count_vec4_slots(var->type, true, false);
      for (uint32_t i = 0; i < slots; i++)
         attributes[var->data.location + i].is_64bit = is_64bit;
   }

   /* First mark all used inputs */
   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            if (intrin->intrinsic != nir_intrinsic_load_input)
               continue;

            assert(intrin->def.bit_size == 32);

            const struct nir_io_semantics io =
               nir_intrinsic_io_semantics(intrin);

            attributes[io.location].is_used = true;

            /* SKL PRMs, Vol 2a: Command Reference: Instructions,
             * 3DSTATE_VF_COMPONENT_PACKING:
             *
             *    "Software shall enable all components (XYZW) for any and all
             *     VERTEX_ELEMENTs associated with a 256-bit SURFACE_FORMAT.
             *     It is INVALID to disable any components in these cases."
             *
             * Enable this XYZW for any > 128-bit format.
             */
            if (nir->info.dual_slot_inputs & BITFIELD64_BIT(io.location)) {
               attributes[io.location].component_mask |= 0xff;
            } else {
               const uint8_t mask =
                  nir_component_mask(intrin->num_components) <<
                  nir_intrinsic_component(intrin);

               attributes[io.location].component_mask |= mask;
            }
         }
      }
   }

   /* SKL PRMs, Vol 2a: Command Reference: Instructions,
    * 3DSTATE_VF_COMPONENT_PACKING:
    *
    *    "At least one component of one "valid" Vertex Element must be
    *     enabled."
    */
   if (nir->info.inputs_read == 0) {
      if (prog_data->no_vf_slot_compaction) {
         attributes[VERT_ATTRIB_GENERIC0].is_used = true;
         attributes[VERT_ATTRIB_GENERIC0].component_mask = 0x1;
      } else if (!BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_IS_INDEXED_DRAW) &&
                 !BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_FIRST_VERTEX) &&
                 !BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_BASE_INSTANCE) &&
                 !BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_VERTEX_ID_ZERO_BASE) &&
                 !BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_INSTANCE_ID) &&
                 !BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_DRAW_ID)) {
         attributes[VERT_ATTRIB_GENERIC0].is_used = true;
         attributes[VERT_ATTRIB_GENERIC0].component_mask = 0x1;
      }
   }

   /* Compute the register offsets */
   unsigned reg_offset = 0;
   unsigned vertex_element = 0;
   for (unsigned a = 0; a < ARRAY_SIZE(attributes); a++) {
      if (!attributes[a].is_used)
         continue;

      /* SKL PRMs, Vol 2a: Command Reference: Instructions,
       * 3DSTATE_VF_COMPONENT_PACKING:
       *
       *    "No enable bits are provided for Vertex Elements [32-33],
       *     and therefore no packing is performed on these elements (if
       *     Valid, all 4 components are stored)."
       */
      if (vertex_element >= 32 ||
          (prog_data->no_vf_slot_compaction && a >= VERT_ATTRIB_GENERIC(32)))
         attributes[a].component_mask = 0xf;

      attributes[a].reg_offset = reg_offset;

      reg_offset += util_bitcount(attributes[a].component_mask);
      vertex_element++;
   }

   /* Remap inputs */
   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            if (intrin->intrinsic != nir_intrinsic_load_input)
               continue;

            struct nir_io_semantics io = nir_intrinsic_io_semantics(intrin);

            unsigned slot = attributes[io.location].reg_offset / 4;
            unsigned slot_component =
               attributes[io.location].reg_offset % 4 +
               util_bitcount(attributes[io.location].component_mask &
                             BITFIELD_MASK(io.high_dvec2 * 4 +
                                           nir_intrinsic_component(intrin)));

            slot += slot_component / 4;
            slot_component %= 4;

            nir_intrinsic_set_base(intrin, slot);
            nir_intrinsic_set_component(intrin, slot_component);

            /* The code above generates load_input with
             * "component + num_component > 4", which is theoretically illegal.
             */
            io.no_validate = 1;
            nir_intrinsic_set_io_semantics(intrin, io);
         }
      }
   }

   /* Generate the packing array, we start from the first application
    * attribute : VERT_ATTRIB_GENERIC0
    */
   unsigned vf_element_count = 0;
   for (unsigned a = VERT_ATTRIB_GENERIC0; a < ARRAY_SIZE(attributes) && vf_element_count < 32; a++) {
      /* Consider all attributes used when no slot compaction is active */
      if (!attributes[a].is_used && !prog_data->no_vf_slot_compaction)
         continue;

      uint32_t mask;
      /* Stores masks in attributes[a].component_mask are in terms of 32-bit
       * components, but the HW depending on the format will interpret
       * prog_data->vf_component_packing[] bits as either a 32-bit or 64-bit
       * component. So we need to only consider every other bit.
       */
      if (attributes[a].is_64bit) {
         mask = 0;
         u_foreach_bit(b, attributes[a].component_mask)
            mask |= BITFIELD_BIT(b / 2);
      } else {
         mask = attributes[a].component_mask;
      }
      /* We should only have 4bits enabled max */
      assert((mask & ~0xfu) == 0);

      prog_data->vf_component_packing[vf_element_count / 8] |=
         mask << (4 * (vf_element_count % 8));
      vf_element_count++;
   }

   nir_validate_shader(nir, __func__);
   return reg_offset;
}

static bool
run_vs(brw_shader &s)
{
   assert(s.stage == MESA_SHADER_VERTEX);

   s.payload_ = new brw_vs_thread_payload(s);

   brw_from_nir(&s);

   if (s.failed)
      return false;

   s.emit_urb_writes();

   brw_calculate_cfg(s);

   brw_optimize(s);

   s.assign_curb_setup();
   brw_assign_vs_urb_setup(s);

   brw_lower_3src_null_dest(s);
   brw_workaround_emit_dummy_mov_instruction(s);

   brw_allocate_registers(s, true /* allow_spilling */);

   brw_workaround_source_arf_before_eot(s);

   return !s.failed;
}

extern "C" const unsigned *
brw_compile_vs(const struct brw_compiler *compiler,
               struct brw_compile_vs_params *params)
{
   struct nir_shader *nir = params->base.nir;
   const struct brw_vs_prog_key *key = params->key;
   struct brw_vs_prog_data *prog_data = params->prog_data;
   const bool debug_enabled =
      brw_should_print_shader(nir, params->base.debug_flag ?
                                   params->base.debug_flag : DEBUG_VS,
                                   params->base.source_hash);
   const unsigned dispatch_width = brw_geometry_stage_dispatch_width(compiler->devinfo);

   /* We only expect slot compaction to be disabled when using device
    * generated commands, to provide an independent 3DSTATE_VERTEX_ELEMENTS
    * programming. This should always be enabled together with VF component
    * packing to minimize the size of the payload.
    */
   assert(!key->no_vf_slot_compaction || key->vf_component_packing);

   brw_debug_archive_nir(params->base.archiver, nir, dispatch_width, "first");

   brw_prog_data_init(&prog_data->base.base, &params->base);

   /* When using Primitive Replication for multiview, each view gets its own
    * position slot.
    */
   const uint32_t pos_slots =
      (nir->info.per_view_outputs & VARYING_BIT_POS) ?
      MAX2(1, util_bitcount(key->base.view_mask)) : 1;

   /* Only position is allowed to be per-view */
   assert(!(nir->info.per_view_outputs & ~VARYING_BIT_POS));

   brw_compute_vue_map(compiler->devinfo,
                       &prog_data->base.vue_map, nir->info.outputs_written,
                       key->base.vue_layout, pos_slots);

   brw_nir_apply_key(nir, compiler, &key->base, dispatch_width);

   prog_data->inputs_read = nir->info.inputs_read;
   prog_data->double_inputs_read = nir->info.vs.double_inputs;
   prog_data->no_vf_slot_compaction = key->no_vf_slot_compaction;

   brw_nir_lower_vs_inputs(nir);
   brw_nir_lower_vue_outputs(nir);

   memset(prog_data->vf_component_packing, 0,
          sizeof(prog_data->vf_component_packing));
   unsigned nr_packed_regs = 0;
   if (key->vf_component_packing)
      nr_packed_regs = brw_nir_pack_vs_input(nir, prog_data);

   brw_postprocess_nir(nir, compiler, dispatch_width,
                       params->base.archiver, debug_enabled,
                       key->base.robust_flags);

   unsigned nr_attribute_slots = util_bitcount64(prog_data->inputs_read);
   /* gl_VertexID and gl_InstanceID are system values, but arrive via an
    * incoming vertex attribute.  So, add an extra slot.
    */
   if (BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_FIRST_VERTEX) ||
       BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_BASE_INSTANCE) ||
       BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_VERTEX_ID_ZERO_BASE) ||
       BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_INSTANCE_ID)) {
      nr_attribute_slots++;
   }

   /* gl_DrawID and IsIndexedDraw share its very own vec4 */
   if (BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_DRAW_ID) ||
       BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_IS_INDEXED_DRAW)) {
      nr_attribute_slots++;
   }

   if (BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_IS_INDEXED_DRAW))
      prog_data->uses_is_indexed_draw = true;

   if (BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_FIRST_VERTEX))
      prog_data->uses_firstvertex = true;

   if (BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_BASE_INSTANCE))
      prog_data->uses_baseinstance = true;

   if (BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_VERTEX_ID_ZERO_BASE))
      prog_data->uses_vertexid = true;

   if (BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_INSTANCE_ID))
      prog_data->uses_instanceid = true;

   if (BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_DRAW_ID))
      prog_data->uses_drawid = true;

   unsigned nr_attribute_regs;
   if (key->vf_component_packing) {
      prog_data->base.urb_read_length = DIV_ROUND_UP(nr_packed_regs, 8);
      nr_attribute_regs = nr_packed_regs;
   } else {
      prog_data->base.urb_read_length = DIV_ROUND_UP(nr_attribute_slots, 2);
      nr_attribute_regs = 4 * (nr_attribute_slots);
   }

   /* Since vertex shaders reuse the same VUE entry for inputs and outputs
    * (overwriting the original contents), we need to make sure the size is
    * the larger of the two.
    */
   const unsigned vue_entries =
      MAX2(DIV_ROUND_UP(nr_attribute_regs, 4),
           (unsigned)prog_data->base.vue_map.num_slots);

   prog_data->base.urb_entry_size = DIV_ROUND_UP(vue_entries, 4);

   if (unlikely(debug_enabled)) {
      fprintf(stderr, "VS Output ");
      brw_print_vue_map(stderr, &prog_data->base.vue_map, MESA_SHADER_VERTEX);
   }

   prog_data->base.dispatch_mode = INTEL_DISPATCH_MODE_SIMD8;

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
   if (!run_vs(v)) {
      params->base.error_str =
         ralloc_strdup(params->base.mem_ctx, v.fail_msg);
      return NULL;
   }

   assert(v.payload().num_regs % reg_unit(compiler->devinfo) == 0);
   prog_data->base.base.dispatch_grf_start_reg =
      v.payload().num_regs / reg_unit(compiler->devinfo);
   prog_data->base.base.grf_used = v.grf_used;

   brw_generator g(compiler, &params->base,
                  &prog_data->base.base,
                  MESA_SHADER_VERTEX);
   if (unlikely(debug_enabled)) {
      const char *debug_name =
         ralloc_asprintf(params->base.mem_ctx, "%s vertex shader %s",
                         nir->info.label ? nir->info.label :
                            "unnamed",
                         nir->info.name);

      g.enable_debug(debug_name);
   }
   g.generate_code(v, params->base.stats);
   g.add_const_data(nir->constant_data, nir->constant_data_size);

   return g.get_assembly();
}
