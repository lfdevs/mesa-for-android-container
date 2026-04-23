/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#include "compiler/brw/brw_compiler.h"
#include "compiler/brw/brw_nir.h"
#include "compiler/intel_nir.h"
#include "jay_private.h"
#include "nir.h"

static inline enum intel_barycentric_mode
brw_barycentric_mode(const struct brw_fs_prog_key *key,
                     nir_intrinsic_instr *intr)
{
   const enum glsl_interp_mode mode = nir_intrinsic_interp_mode(intr);

   /* Barycentric modes don't make sense for flat inputs. */
   assert(mode != INTERP_MODE_FLAT);

   unsigned bary;
   switch (intr->intrinsic) {
   case nir_intrinsic_load_barycentric_pixel:
   case nir_intrinsic_load_barycentric_at_offset:
      /* When per sample interpolation is dynamic, assume sample interpolation.
       * We'll dynamically remap things so that the FS payload is not affected.
       */
      bary = key->persample_interp == INTEL_SOMETIMES ?
                INTEL_BARYCENTRIC_PERSPECTIVE_SAMPLE :
                INTEL_BARYCENTRIC_PERSPECTIVE_PIXEL;
      break;
   case nir_intrinsic_load_barycentric_centroid:
      bary = INTEL_BARYCENTRIC_PERSPECTIVE_CENTROID;
      break;
   case nir_intrinsic_load_barycentric_sample:
   case nir_intrinsic_load_barycentric_at_sample:
      bary = INTEL_BARYCENTRIC_PERSPECTIVE_SAMPLE;
      break;
   default:
      UNREACHABLE("invalid intrinsic");
   }

   if (mode == INTERP_MODE_NOPERSPECTIVE)
      bary += 3;

   return (enum intel_barycentric_mode) bary;
}

struct fs_info_ctx {
   const struct brw_fs_prog_key *key;
   struct brw_fs_prog_data *prog_data;
   const struct intel_device_info *devinfo;
};

static bool
gather_fs_info(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   struct fs_info_ctx *ctx = data;
   struct brw_fs_prog_data *prog_data = ctx->prog_data;

   switch (intr->intrinsic) {
   case nir_intrinsic_load_barycentric_pixel:
   case nir_intrinsic_load_barycentric_centroid:
   case nir_intrinsic_load_barycentric_sample:
      prog_data->barycentric_interp_modes |=
         1 << brw_barycentric_mode(ctx->key, intr);
      break;

   case nir_intrinsic_load_barycentric_at_sample:
   case nir_intrinsic_load_barycentric_at_offset: {
      unsigned mode = brw_barycentric_mode(ctx->key, intr);
      prog_data->barycentric_interp_modes |= 1 << mode;
      prog_data->uses_sample_offsets |=
         mode == INTEL_BARYCENTRIC_PERSPECTIVE_SAMPLE ||
         mode == INTEL_BARYCENTRIC_NONPERSPECTIVE_SAMPLE;

      if ((1 << mode) & INTEL_BARYCENTRIC_NONPERSPECTIVE_BITS)
         prog_data->uses_npc_bary_coefficients = true;
      else
         prog_data->uses_pc_bary_coefficients = true;
      break;
   }

   case nir_intrinsic_load_frag_coord_z:
      prog_data->uses_src_depth = true;
      break;

   case nir_intrinsic_load_frag_coord_w_rcp:
      prog_data->uses_src_w = true;
      break;

   case nir_intrinsic_load_sample_mask_in:
      /* TODO: Sample masks are broken and discards are broken and simd32
       * layouts are broken too. XXX.
       */
      // prog_data->uses_sample_mask = true;
      break;

   case nir_intrinsic_load_pixel_coord_intel:
      BITSET_SET(b->shader->info.system_values_read, SYSTEM_VALUE_FRAG_COORD);
      break;

   default:
      break;
   }

   return false;
}

static void
brw_compute_flat_inputs(struct brw_fs_prog_data *prog_data,
                        const nir_shader *shader)
{
   prog_data->flat_inputs = 0;

   nir_foreach_shader_in_variable(var, shader) {
      if (var->data.interpolation != INTERP_MODE_FLAT ||
          var->data.per_primitive)
         continue;

      unsigned slots = glsl_count_attribute_slots(var->type, false);
      for (unsigned s = 0; s < slots; s++) {
         int input_index = prog_data->urb_setup[var->data.location + s];

         if (input_index >= 0)
            prog_data->flat_inputs |= 1 << input_index;
      }
   }
}

static uint8_t
computed_depth_mode(const nir_shader *shader)
{
   if (shader->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_DEPTH)) {
      switch (shader->info.fs.depth_layout) {
      case FRAG_DEPTH_LAYOUT_NONE:
      case FRAG_DEPTH_LAYOUT_ANY:
         return BRW_PSCDEPTH_ON;
      case FRAG_DEPTH_LAYOUT_GREATER:
         return BRW_PSCDEPTH_ON_GE;
      case FRAG_DEPTH_LAYOUT_LESS:
         return BRW_PSCDEPTH_ON_LE;
      case FRAG_DEPTH_LAYOUT_UNCHANGED:
         /* We initially set this to OFF, but having the shader write the
          * depth means we allocate register space in the SEND message. The
          * difference between the SEND register count and the OFF state
          * programming makes the HW hang.
          *
          * Removing the depth writes also leads to test failures. So use
          * LesserThanOrEqual, which fits writing the same value
          * (unchanged/equal).
          *
          */
         return BRW_PSCDEPTH_ON_LE;
      }
   }
   return BRW_PSCDEPTH_OFF;
}

/*
 * Build up an array of indices into the urb_setup array that
 * references the active entries of the urb_setup array.
 * Used to accelerate walking the active entries of the urb_setup array
 * on each upload.
 */
static void
brw_compute_urb_setup_index(struct brw_fs_prog_data *fs_prog_data)
{
   /* TODO(mesh): Review usage of this in the context of Mesh, we may want to
    * skip per-primitive attributes here.
    */

   /* Make sure uint8_t is sufficient */
   static_assert(VARYING_SLOT_MAX <= 0xff);
   uint8_t index = 0;
   for (uint8_t attr = 0; attr < VARYING_SLOT_MAX; attr++) {
      if (fs_prog_data->urb_setup[attr] >= 0) {
         fs_prog_data->urb_setup_attribs[index++] = attr;
      }
   }
   fs_prog_data->urb_setup_attribs_count = index;
}

static void
calculate_urb_setup(const struct intel_device_info *devinfo,
                    const struct brw_fs_prog_key *key,
                    struct brw_fs_prog_data *prog_data,
                    nir_shader *nir,
                    const struct brw_mue_map *mue_map,
                    int *per_primitive_offsets)
{
   memset(prog_data->urb_setup, -1, sizeof(prog_data->urb_setup));
   int urb_next = 0; /* in vec4s */

   /* Figure out where the PrimitiveID lives, either in the per-vertex block
    * or in the per-primitive block or both.
    */
   const uint64_t per_vert_primitive_id =
      key->mesh_input == INTEL_ALWAYS ? 0 : VARYING_BIT_PRIMITIVE_ID;
   const uint64_t per_prim_primitive_id =
      key->mesh_input == INTEL_NEVER ? 0 : VARYING_BIT_PRIMITIVE_ID;
   const uint64_t inputs_read =
      nir->info.inputs_read &
      (~nir->info.per_primitive_inputs | per_vert_primitive_id);
   const uint64_t per_primitive_header_bits =
      VARYING_BIT_PRIMITIVE_SHADING_RATE |
      VARYING_BIT_LAYER |
      VARYING_BIT_VIEWPORT |
      VARYING_BIT_CULL_PRIMITIVE;
   const uint64_t per_primitive_inputs =
      nir->info.inputs_read &
      (nir->info.per_primitive_inputs | per_prim_primitive_id) &
      ~per_primitive_header_bits;
   struct intel_vue_map vue_map;
   uint32_t per_primitive_stride = 0, first_read_offset = UINT32_MAX;

   if (mue_map != NULL) {
      memcpy(&vue_map, &mue_map->vue_map, sizeof(vue_map));
      memcpy(per_primitive_offsets, mue_map->per_primitive_offsets,
             sizeof(mue_map->per_primitive_offsets));

      if (!mue_map->wa_18019110168_active) {
         u_foreach_bit64(location, per_primitive_inputs) {
            assert(per_primitive_offsets[location] != -1);

            first_read_offset =
               MIN2(first_read_offset,
                    (uint32_t) per_primitive_offsets[location]);
            per_primitive_stride =
               MAX2((uint32_t) per_primitive_offsets[location] + 16,
                    per_primitive_stride);
         }
      } else {
         first_read_offset = per_primitive_stride = 0;
      }
   } else {
      brw_compute_vue_map(devinfo, &vue_map, inputs_read, key->base.vue_layout,
                          1 /* pos_slots, TODO */);
      brw_compute_per_primitive_map(per_primitive_offsets,
                                    &per_primitive_stride, &first_read_offset,
                                    0, nir, nir_var_shader_in,
                                    per_primitive_inputs,
                                    true /* separate_shader */);
   }

   if (per_primitive_stride > first_read_offset) {
      first_read_offset = ROUND_DOWN_TO(first_read_offset, 32);

      /* Remove the first few unused registers */
      for (uint32_t i = 0; i < VARYING_SLOT_MAX; i++) {
         if (per_primitive_offsets[i] == -1)
            continue;
         per_primitive_offsets[i] -= first_read_offset;
      }

      prog_data->num_per_primitive_inputs =
         2 * DIV_ROUND_UP(per_primitive_stride - first_read_offset, 32);
   } else {
      prog_data->num_per_primitive_inputs = 0;
   }

   /* Now do the per-vertex stuff (what used to be legacy pipeline) */

   /* If Mesh is involved, we cannot do any packing. Documentation doesn't say
    * anything about this but 3DSTATE_SBE_SWIZ does not appear to work when
    * using Mesh.
    */
   if (util_bitcount64(inputs_read) <= 16 && key->mesh_input == INTEL_NEVER) {
      /* When not in Mesh pipeline mode, the SF/SBE pipeline stage can do
       * arbitrary rearrangement of the first 16 varying inputs, so we can put
       * them wherever we want. Just put them in order.
       *
       * This is useful because it means that (a) inputs not used by the
       * fragment shader won't take up valuable register space, and (b) we
       * won't have to recompile the fragment shader if it gets paired with a
       * different vertex (or geometry) shader.
       */
      for (unsigned int i = 0; i < VARYING_SLOT_MAX; i++) {
         if (inputs_read & BITFIELD64_BIT(i)) {
            prog_data->urb_setup[i] = urb_next++;
         }
      }
   } else {
      /* We have enough input varyings that the SF/SBE pipeline stage can't
       * arbitrarily rearrange them to suit our whim; we have to put them in
       * an order that matches the output of the previous pipeline stage
       * (geometry or vertex shader).
       */
      int first_slot = 0;
      for (int i = 0; i < vue_map.num_slots; i++) {
         int varying = vue_map.slot_to_varying[i];
         if (varying > 0 && (inputs_read & BITFIELD64_BIT(varying)) != 0) {
            first_slot = ROUND_DOWN_TO(i, 2);
            break;
         }
      }

      for (int slot = first_slot; slot < vue_map.num_slots; slot++) {
         int varying = vue_map.slot_to_varying[slot];
         if (varying > 0 && (inputs_read & BITFIELD64_BIT(varying))) {
            prog_data->urb_setup[varying] = slot - first_slot;
         }
      }
      urb_next = vue_map.num_slots - first_slot;
   }

   prog_data->num_varying_inputs = urb_next;
   prog_data->inputs = inputs_read;
   prog_data->per_primitive_inputs = per_primitive_inputs;

   brw_compute_urb_setup_index(prog_data);
}

static void
populate_fs_prog_data(nir_shader *shader,
                      const struct intel_device_info *devinfo,
                      const struct brw_fs_prog_key *key,
                      struct brw_fs_prog_data *prog_data,
                      const struct brw_mue_map *mue_map,
                      int *per_primitive_offsets)
{
   struct fs_info_ctx ctx = {
      .key = key,
      .prog_data = prog_data,
      .devinfo = devinfo,
   };
   nir_shader_intrinsics_pass(shader, gather_fs_info, nir_metadata_all, &ctx);

   prog_data->uses_kill = shader->info.fs.uses_discard;
   prog_data->uses_omask =
      !key->ignore_sample_mask_out &&
      (shader->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_SAMPLE_MASK));
   prog_data->max_polygons = 1;
   prog_data->computed_depth_mode = computed_depth_mode(shader);
   prog_data->computed_stencil =
      shader->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_STENCIL);

   prog_data->sample_shading = shader->info.fs.uses_sample_shading;
   prog_data->api_sample_shading = key->api_sample_shading;
   prog_data->min_sample_shading = key->min_sample_shading;

   assert(key->multisample_fbo != INTEL_NEVER ||
          key->persample_interp == INTEL_NEVER);

   prog_data->persample_dispatch = key->persample_interp;
   if (prog_data->sample_shading)
      prog_data->persample_dispatch = INTEL_ALWAYS;

   /* We can only persample dispatch if we have a multisample FBO */
   prog_data->persample_dispatch =
      MIN2(prog_data->persample_dispatch, key->multisample_fbo);

   /* Currently only the Vulkan API allows alpha_to_coverage to be dynamic. If
    * persample_dispatch & multisample_fbo are not dynamic, Anv should be able
    * to definitively tell whether alpha_to_coverage is on or off.
    */
   prog_data->alpha_to_coverage = key->alpha_to_coverage;

   assert(devinfo->verx10 >= 125 || key->mesh_input == INTEL_NEVER);
   prog_data->mesh_input = key->mesh_input;

   assert(devinfo->verx10 >= 200 || key->provoking_vertex_last == INTEL_NEVER);
   prog_data->provoking_vertex_last = key->provoking_vertex_last;

   /* From the Ivy Bridge PRM documentation for 3DSTATE_PS:
    *
    *    "MSDISPMODE_PERSAMPLE is required in order to select
    *    POSOFFSET_SAMPLE"
    *
    * So we can only really get sample positions if we are doing real
    * per-sample dispatch.  If we need gl_SamplePosition and we don't have
    * persample dispatch, we hard-code it to 0.5.
    */
   prog_data->uses_pos_offset =
      prog_data->persample_dispatch != INTEL_NEVER &&
      (BITSET_TEST(shader->info.system_values_read, SYSTEM_VALUE_SAMPLE_POS) ||
       BITSET_TEST(shader->info.system_values_read,
                   SYSTEM_VALUE_SAMPLE_POS_OR_CENTER));

   prog_data->early_fragment_tests = shader->info.fs.early_fragment_tests;
   prog_data->post_depth_coverage = shader->info.fs.post_depth_coverage;
   prog_data->inner_coverage = shader->info.fs.inner_coverage;

   /* From the BDW PRM documentation for 3DSTATE_WM:
    *
    *    "MSDISPMODE_PERSAMPLE is required in order to select Perspective
    *     Sample or Non- perspective Sample barycentric coordinates."
    *
    * So cleanup any potentially set sample barycentric mode when not in per
    * sample dispatch.
    */
   if (prog_data->persample_dispatch == INTEL_NEVER) {
      prog_data->barycentric_interp_modes &=
         ~BITFIELD_BIT(INTEL_BARYCENTRIC_PERSPECTIVE_SAMPLE);
   }

   if (devinfo->ver >= 20) {
      prog_data->vertex_attributes_bypass =
         brw_needs_vertex_attributes_bypass(shader);
   }

   prog_data->uses_nonperspective_interp_modes =
      (prog_data->barycentric_interp_modes &
       INTEL_BARYCENTRIC_NONPERSPECTIVE_BITS) ||
      prog_data->uses_npc_bary_coefficients;

   /* The current VK_EXT_graphics_pipeline_library specification requires
    * coarse to specified at compile time. But per sample interpolation can be
    * dynamic. So we should never be in a situation where coarse &
    * persample_interp are both respectively true & INTEL_ALWAYS.
    *
    * Coarse will dynamically turned off when persample_interp is active.
    */
   assert(!key->coarse_pixel || key->persample_interp != INTEL_ALWAYS);

   prog_data->coarse_pixel_dispatch =
      intel_sometimes_invert(prog_data->persample_dispatch);
   if (!key->coarse_pixel ||
       /* DG2 should support this, but Wa_22012766191 says there are issues
        * with CPS 1x1 + MSAA + FS writing to oMask.
        */
       (devinfo->verx10 < 200 &&
        (prog_data->uses_omask || prog_data->uses_sample_mask)) ||
       prog_data->sample_shading ||
       (prog_data->computed_depth_mode != BRW_PSCDEPTH_OFF) ||
       prog_data->computed_stencil ||
       devinfo->ver < 11) {
      prog_data->coarse_pixel_dispatch = INTEL_NEVER;
   }

   /* ICL PRMs, Volume 9: Render Engine, Shared Functions Pixel Interpolater,
    * Message Descriptor :
    *
    *    "Message Type. Specifies the type of message being sent when
    *     pixel-rate evaluation is requested :
    *
    *     Format = U2
    *       0: Per Message Offset (eval_snapped with immediate offset)
    *       1: Sample Position Offset (eval_sindex)
    *       2: Centroid Position Offset (eval_centroid)
    *       3: Per Slot Offset (eval_snapped with register offset)
    *
    *     Message Type. Specifies the type of message being sent when
    *     coarse-rate evaluation is requested :
    *
    *     Format = U2
    *       0: Coarse to Pixel Mapping Message (internal message)
    *       1: Reserved
    *       2: Coarse Centroid Position (eval_centroid)
    *       3: Per Slot Coarse Pixel Offset (eval_snapped with register offset)"
    *
    * The Sample Position Offset is marked as reserved for coarse rate
    * evaluation and leads to hangs if we try to use it. So disable coarse
    * pixel shading if we have any intrinsic that will result in a pixel
    * interpolater message at sample.
    */
   if (intel_nir_pulls_at_sample(shader))
      prog_data->coarse_pixel_dispatch = INTEL_NEVER;

   /* We choose to always enable VMask prior to XeHP, as it would cause
    * us to lose out on the eliminate_find_live_channel() optimization.
    */
   prog_data->uses_vmask =
      devinfo->verx10 < 125 ||
      shader->info.fs.needs_coarse_quad_helper_invocations ||
      shader->info.uses_wide_subgroup_intrinsics ||
      prog_data->coarse_pixel_dispatch != INTEL_NEVER;

   prog_data->uses_depth_w_coefficients = prog_data->uses_pc_bary_coefficients;

   if (prog_data->coarse_pixel_dispatch != INTEL_NEVER) {
      prog_data->uses_depth_w_coefficients |= prog_data->uses_src_depth;
      prog_data->uses_src_depth = false;
   }

   calculate_urb_setup(devinfo, key, prog_data, shader, mue_map,
                       per_primitive_offsets);
   brw_compute_flat_inputs(prog_data, shader);

   prog_data->has_side_effects = shader->info.writes_memory;
}

static void
populate_vs_prog_data(nir_shader *nir,
                      const struct intel_device_info *devinfo,
                      const struct brw_vs_prog_key *key,
                      struct brw_vs_prog_data *prog_data,
                      unsigned nr_packed_regs)
{
   unsigned nr_attribute_slots = util_bitcount64(prog_data->inputs_read);
   BITSET_WORD *sysvals = nir->info.system_values_read;

   /* gl_VertexID and gl_InstanceID are system values, but arrive via an
    * incoming vertex attribute.  So, add an extra slot.
    */
   if (BITSET_TEST(sysvals, SYSTEM_VALUE_FIRST_VERTEX) ||
       BITSET_TEST(sysvals, SYSTEM_VALUE_BASE_INSTANCE) ||
       BITSET_TEST(sysvals, SYSTEM_VALUE_VERTEX_ID_ZERO_BASE) ||
       BITSET_TEST(sysvals, SYSTEM_VALUE_INSTANCE_ID)) {
      nr_attribute_slots++;
   }

   /* gl_DrawID and IsIndexedDraw share its very own vec4 */
   if (BITSET_TEST(sysvals, SYSTEM_VALUE_DRAW_ID) ||
       BITSET_TEST(sysvals, SYSTEM_VALUE_IS_INDEXED_DRAW)) {
      nr_attribute_slots++;
   }

   const struct {
      bool *data;
      gl_system_value val;
   } bool_sysvals[] = {
      { &prog_data->uses_is_indexed_draw, SYSTEM_VALUE_IS_INDEXED_DRAW     },
      { &prog_data->uses_firstvertex,     SYSTEM_VALUE_FIRST_VERTEX        },
      { &prog_data->uses_baseinstance,    SYSTEM_VALUE_BASE_INSTANCE       },
      { &prog_data->uses_vertexid,        SYSTEM_VALUE_VERTEX_ID_ZERO_BASE },
      { &prog_data->uses_instanceid,      SYSTEM_VALUE_INSTANCE_ID         },
      { &prog_data->uses_drawid,          SYSTEM_VALUE_DRAW_ID             },
   };

   for (unsigned i = 0; i < ARRAY_SIZE(bool_sysvals); ++i) {
      *bool_sysvals[i].data = BITSET_TEST(sysvals, bool_sysvals[i].val);
   }

   unsigned nr_attribute_regs;
   if (key->vf_component_packing) {
      prog_data->base.urb_read_length = DIV_ROUND_UP(nr_packed_regs, 8);
      nr_attribute_regs = nr_packed_regs;
   } else {
      prog_data->base.urb_read_length = DIV_ROUND_UP(nr_attribute_slots, 2);
      nr_attribute_regs = 4 * nr_attribute_slots;
   }

   /* Since vertex shaders reuse the same VUE entry for inputs and outputs
    * (overwriting the original contents), we need to make sure the size is
    * the larger of the two.
    */
   const unsigned vue_entries = MAX2(DIV_ROUND_UP(nr_attribute_regs, 4),
                                     prog_data->base.vue_map.num_slots);
   prog_data->base.urb_entry_size = DIV_ROUND_UP(vue_entries, 4);
   prog_data->base.dispatch_mode = INTEL_DISPATCH_MODE_SIMD8;
}

void
jay_populate_prog_data(const struct intel_device_info *devinfo,
                       nir_shader *nir,
                       union brw_any_prog_data *prog_data,
                       union brw_any_prog_key *key,
                       unsigned nr_packed_regs)
{
   if (nir->info.stage == MESA_SHADER_VERTEX) {
      populate_vs_prog_data(nir, devinfo, &key->vs, &prog_data->vs,
                            nr_packed_regs);
   } else if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      int per_primitive_offsets[VARYING_SLOT_MAX];
      memset(per_primitive_offsets, -1, sizeof(per_primitive_offsets));

      populate_fs_prog_data(nir, devinfo, &key->fs, &prog_data->fs,
                            NULL /* TODO: mue_map */, per_primitive_offsets);
   } else if (mesa_shader_stage_is_compute(nir->info.stage)) {
      prog_data->cs.uses_inline_push_addr = key->base.uses_inline_push_addr;
      prog_data->cs.uses_inline_data |= key->base.uses_inline_push_addr;
   }

   if (nir->info.stage == MESA_SHADER_VERTEX ||
       nir->info.stage == MESA_SHADER_TESS_EVAL ||
       nir->info.stage == MESA_SHADER_GEOMETRY ||
       nir->info.stage == MESA_SHADER_MESH) {

      uint32_t clip_mask = BITFIELD_MASK(nir->info.clip_distance_array_size);
      uint32_t cull_mask = BITFIELD_RANGE(nir->info.clip_distance_array_size,
                                          nir->info.cull_distance_array_size);

      if (nir->info.stage == MESA_SHADER_MESH) {
         prog_data->mesh.clip_distance_mask = clip_mask;
         prog_data->mesh.cull_distance_mask = cull_mask;
      } else {
         prog_data->vue.clip_distance_mask = clip_mask;
         prog_data->vue.cull_distance_mask = cull_mask;
      }
   }
}
