/*
 * Copyright © 2017 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * @file iris_program.c
 *
 * This file contains the driver interface for compiling shaders.
 *
 * See iris_program_cache.c for the in-memory program cache where the
 * compiled shaders are stored.
 */

#include <stdio.h>
#include <errno.h>
#include "pipe/p_defines.h"
#include "pipe/p_state.h"
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "util/u_atomic.h"
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "intel/compiler/brw_compiler.h"
#include "intel/compiler/brw_nir.h"
#include "iris_context.h"
#include "nir/tgsi_to_nir.h"

#define KEY_INIT_NO_ID(gen)                       \
   .tex.swizzles[0 ... MAX_SAMPLERS - 1] = 0x688, \
   .tex.compressed_multisample_layout_mask = ~0,  \
   .tex.msaa_16 = (gen >= 9 ? ~0 : 0)
#define KEY_INIT(gen) .program_string_id = ish->program_id, KEY_INIT_NO_ID(gen)

static unsigned
get_new_program_id(struct iris_screen *screen)
{
   return p_atomic_inc_return(&screen->program_id);
}

/**
 * An uncompiled, API-facing shader.  This is the Gallium CSO for shaders.
 * It primarily contains the NIR for the shader.
 *
 * Each API-facing shader can be compiled into multiple shader variants,
 * based on non-orthogonal state dependencies, recorded in the shader key.
 *
 * See iris_compiled_shader, which represents a compiled shader variant.
 */
struct iris_uncompiled_shader {
   nir_shader *nir;

   struct pipe_stream_output_info stream_output;

   unsigned program_id;

   /** Bitfield of (1 << IRIS_NOS_*) flags. */
   unsigned nos;

   /** Have any shader variants been compiled yet? */
   bool compiled_once;
};

static nir_ssa_def *
get_aoa_deref_offset(nir_builder *b,
                     nir_deref_instr *deref,
                     unsigned elem_size)
{
   unsigned array_size = elem_size;
   nir_ssa_def *offset = nir_imm_int(b, 0);

   while (deref->deref_type != nir_deref_type_var) {
      assert(deref->deref_type == nir_deref_type_array);

      /* This level's element size is the previous level's array size */
      nir_ssa_def *index = nir_ssa_for_src(b, deref->arr.index, 1);
      assert(deref->arr.index.ssa);
      offset = nir_iadd(b, offset,
                           nir_imul(b, index, nir_imm_int(b, array_size)));

      deref = nir_deref_instr_parent(deref);
      assert(glsl_type_is_array(deref->type));
      array_size *= glsl_get_length(deref->type);
   }

   /* Accessing an invalid surface index with the dataport can result in a
    * hang.  According to the spec "if the index used to select an individual
    * element is negative or greater than or equal to the size of the array,
    * the results of the operation are undefined but may not lead to
    * termination" -- which is one of the possible outcomes of the hang.
    * Clamp the index to prevent access outside of the array bounds.
    */
   return nir_umin(b, offset, nir_imm_int(b, array_size - elem_size));
}

static void
iris_lower_storage_image_derefs(nir_shader *nir)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);

   nir_builder b;
   nir_builder_init(&b, impl);

   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         switch (intrin->intrinsic) {
         case nir_intrinsic_image_deref_load:
         case nir_intrinsic_image_deref_store:
         case nir_intrinsic_image_deref_atomic_add:
         case nir_intrinsic_image_deref_atomic_min:
         case nir_intrinsic_image_deref_atomic_max:
         case nir_intrinsic_image_deref_atomic_and:
         case nir_intrinsic_image_deref_atomic_or:
         case nir_intrinsic_image_deref_atomic_xor:
         case nir_intrinsic_image_deref_atomic_exchange:
         case nir_intrinsic_image_deref_atomic_comp_swap:
         case nir_intrinsic_image_deref_size:
         case nir_intrinsic_image_deref_samples:
         case nir_intrinsic_image_deref_load_raw_intel:
         case nir_intrinsic_image_deref_store_raw_intel: {
            nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
            nir_variable *var = nir_deref_instr_get_variable(deref);

            b.cursor = nir_before_instr(&intrin->instr);
            nir_ssa_def *index =
               nir_iadd(&b, nir_imm_int(&b, var->data.driver_location),
                            get_aoa_deref_offset(&b, deref, 1));
            nir_rewrite_image_intrinsic(intrin, index, false);
            break;
         }

         default:
            break;
         }
      }
   }
}

// XXX: need unify_interfaces() at link time...

/**
 * Fix an uncompiled shader's stream output info.
 *
 * Core Gallium stores output->register_index as a "slot" number, where
 * slots are assigned consecutively to all outputs in info->outputs_written.
 * This naive packing of outputs doesn't work for us - we too have slots,
 * but the layout is defined by the VUE map, which we won't have until we
 * compile a specific shader variant.  So, we remap these and simply store
 * VARYING_SLOT_* in our copy's output->register_index fields.
 *
 * We also fix up VARYING_SLOT_{LAYER,VIEWPORT,PSIZ} to select the Y/Z/W
 * components of our VUE header.  See brw_vue_map.c for the layout.
 */
static void
update_so_info(struct pipe_stream_output_info *so_info,
               uint64_t outputs_written)
{
   uint8_t reverse_map[64] = {};
   unsigned slot = 0;
   while (outputs_written) {
      reverse_map[slot++] = u_bit_scan64(&outputs_written);
   }

   for (unsigned i = 0; i < so_info->num_outputs; i++) {
      struct pipe_stream_output *output = &so_info->output[i];

      /* Map Gallium's condensed "slots" back to real VARYING_SLOT_* enums */
      output->register_index = reverse_map[output->register_index];

      /* The VUE header contains three scalar fields packed together:
       * - gl_PointSize is stored in VARYING_SLOT_PSIZ.w
       * - gl_Layer is stored in VARYING_SLOT_PSIZ.y
       * - gl_ViewportIndex is stored in VARYING_SLOT_PSIZ.z
       */
      switch (output->register_index) {
      case VARYING_SLOT_LAYER:
         assert(output->num_components == 1);
         output->register_index = VARYING_SLOT_PSIZ;
         output->start_component = 1;
         break;
      case VARYING_SLOT_VIEWPORT:
         assert(output->num_components == 1);
         output->register_index = VARYING_SLOT_PSIZ;
         output->start_component = 2;
         break;
      case VARYING_SLOT_PSIZ:
         assert(output->num_components == 1);
         output->start_component = 3;
         break;
      }

      //info->outputs_written |= 1ull << output->register_index;
   }
}

/**
 * Sets up the starting offsets for the groups of binding table entries
 * common to all pipeline stages.
 *
 * Unused groups are initialized to 0xd0d0d0d0 to make it obvious that they're
 * unused but also make sure that addition of small offsets to them will
 * trigger some of our asserts that surface indices are < BRW_MAX_SURFACES.
 */
static uint32_t
assign_common_binding_table_offsets(const struct gen_device_info *devinfo,
                                    const struct nir_shader *nir,
                                    struct brw_stage_prog_data *prog_data,
                                    uint32_t next_binding_table_offset,
                                    unsigned num_system_values,
                                    unsigned num_cbufs)
{
   const struct shader_info *info = &nir->info;

   unsigned num_textures = util_last_bit(info->textures_used);

   if (num_textures) {
      prog_data->binding_table.texture_start = next_binding_table_offset;
      prog_data->binding_table.gather_texture_start = next_binding_table_offset;
      next_binding_table_offset += num_textures;
   } else {
      prog_data->binding_table.texture_start = 0xd0d0d0d0;
      prog_data->binding_table.gather_texture_start = 0xd0d0d0d0;
   }

   if (info->num_images) {
      prog_data->binding_table.image_start = next_binding_table_offset;
      next_binding_table_offset += info->num_images;
   } else {
      prog_data->binding_table.image_start = 0xd0d0d0d0;
   }

   if (num_cbufs) {
      //assert(info->num_ubos <= BRW_MAX_UBO);
      prog_data->binding_table.ubo_start = next_binding_table_offset;
      next_binding_table_offset += num_cbufs;
   } else {
      prog_data->binding_table.ubo_start = 0xd0d0d0d0;
   }

   if (info->num_ssbos || info->num_abos) {
      prog_data->binding_table.ssbo_start = next_binding_table_offset;
      // XXX: see iris_state "wasting 16 binding table slots for ABOs" comment
      next_binding_table_offset += IRIS_MAX_ABOS + info->num_ssbos;
   } else {
      prog_data->binding_table.ssbo_start = 0xd0d0d0d0;
   }

   prog_data->binding_table.shader_time_start = 0xd0d0d0d0;

   /* Plane 0 is just the regular texture section */
   prog_data->binding_table.plane_start[0] = prog_data->binding_table.texture_start;

   prog_data->binding_table.plane_start[1] = next_binding_table_offset;
   next_binding_table_offset += num_textures;

   prog_data->binding_table.plane_start[2] = next_binding_table_offset;
   next_binding_table_offset += num_textures;

   /* Set the binding table size */
   prog_data->binding_table.size_bytes = next_binding_table_offset * 4;

   return next_binding_table_offset;
}

static void
setup_vec4_image_sysval(uint32_t *sysvals, uint32_t idx,
                        unsigned offset, unsigned n)
{
   assert(offset % sizeof(uint32_t) == 0);

   for (unsigned i = 0; i < n; ++i)
      sysvals[i] = BRW_PARAM_IMAGE(idx, offset / sizeof(uint32_t) + i);

   for (unsigned i = n; i < 4; ++i)
      sysvals[i] = BRW_PARAM_BUILTIN_ZERO;
}

/**
 * Associate NIR uniform variables with the prog_data->param[] mechanism
 * used by the backend.  Also, decide which UBOs we'd like to push in an
 * ideal situation (though the backend can reduce this).
 */
static void
iris_setup_uniforms(const struct brw_compiler *compiler,
                    void *mem_ctx,
                    nir_shader *nir,
                    struct brw_stage_prog_data *prog_data,
                    enum brw_param_builtin **out_system_values,
                    unsigned *out_num_system_values,
                    unsigned *out_num_cbufs)
{
   UNUSED const struct gen_device_info *devinfo = compiler->devinfo;

   /* The intel compiler assumes that num_uniforms is in bytes. For
    * scalar that means 4 bytes per uniform slot.
    *
    * Ref: brw_nir_lower_uniforms, type_size_scalar_bytes.
    */
   nir->num_uniforms *= 4;

   const unsigned IRIS_MAX_SYSTEM_VALUES =
      PIPE_MAX_SHADER_IMAGES * BRW_IMAGE_PARAM_SIZE;
   enum brw_param_builtin *system_values =
      rzalloc_array(mem_ctx, enum brw_param_builtin, IRIS_MAX_SYSTEM_VALUES);
   unsigned num_system_values = 0;

   unsigned patch_vert_idx = -1;
   unsigned ucp_idx[IRIS_MAX_CLIP_PLANES];
   unsigned img_idx[PIPE_MAX_SHADER_IMAGES];
   memset(ucp_idx, -1, sizeof(ucp_idx));
   memset(img_idx, -1, sizeof(img_idx));

   nir_function_impl *impl = nir_shader_get_entrypoint(nir);

   nir_builder b;
   nir_builder_init(&b, impl);

   b.cursor = nir_before_block(nir_start_block(impl));
   nir_ssa_def *temp_ubo_name = nir_ssa_undef(&b, 1, 32);

   /* Turn system value intrinsics into uniforms */
   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         nir_ssa_def *offset;

         switch (intrin->intrinsic) {
         case nir_intrinsic_load_user_clip_plane: {
            unsigned ucp = nir_intrinsic_ucp_id(intrin);

            if (ucp_idx[ucp] == -1) {
               ucp_idx[ucp] = num_system_values;
               num_system_values += 4;
            }

            for (int i = 0; i < 4; i++) {
               system_values[ucp_idx[ucp] + i] =
                  BRW_PARAM_BUILTIN_CLIP_PLANE(ucp, i);
            }

            b.cursor = nir_before_instr(instr);
            offset = nir_imm_int(&b, ucp_idx[ucp] * sizeof(uint32_t));
            break;
         }
         case nir_intrinsic_load_patch_vertices_in:
            if (patch_vert_idx == -1)
               patch_vert_idx = num_system_values++;

            system_values[patch_vert_idx] =
               BRW_PARAM_BUILTIN_PATCH_VERTICES_IN;

            b.cursor = nir_before_instr(instr);
            offset = nir_imm_int(&b, patch_vert_idx * sizeof(uint32_t));
            break;
         case nir_intrinsic_image_deref_load_param_intel: {
            assert(devinfo->gen < 9);
            nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
            nir_variable *var = nir_deref_instr_get_variable(deref);

            /* XXX: var->data.binding is not set properly.  We need to run
             * some form of gl_nir_lower_samplers_as_deref() to get it.
             * This breaks tests which use more than one image.
             */
            if (img_idx[var->data.binding] == -1) {
               /* GL only allows arrays of arrays of images. */
               assert(glsl_type_is_image(glsl_without_array(var->type)));
               unsigned num_images = MAX2(1, glsl_get_aoa_size(var->type));

               for (int i = 0; i < num_images; i++) {
                  const unsigned img = var->data.binding + i;

                  img_idx[img] = num_system_values;
                  num_system_values += BRW_IMAGE_PARAM_SIZE;

                  uint32_t *img_sv = &system_values[img_idx[img]];

                  setup_vec4_image_sysval(
                     img_sv + BRW_IMAGE_PARAM_OFFSET_OFFSET, img,
                     offsetof(struct brw_image_param, offset), 2);
                  setup_vec4_image_sysval(
                     img_sv + BRW_IMAGE_PARAM_SIZE_OFFSET, img,
                     offsetof(struct brw_image_param, size), 3);
                  setup_vec4_image_sysval(
                     img_sv + BRW_IMAGE_PARAM_STRIDE_OFFSET, img,
                     offsetof(struct brw_image_param, stride), 4);
                  setup_vec4_image_sysval(
                     img_sv + BRW_IMAGE_PARAM_TILING_OFFSET, img,
                     offsetof(struct brw_image_param, tiling), 3);
                  setup_vec4_image_sysval(
                     img_sv + BRW_IMAGE_PARAM_SWIZZLING_OFFSET, img,
                     offsetof(struct brw_image_param, swizzling), 2);
               }
            }

            b.cursor = nir_before_instr(instr);
            offset = nir_iadd(&b,
               get_aoa_deref_offset(&b, deref, BRW_IMAGE_PARAM_SIZE * 4),
               nir_imm_int(&b, img_idx[var->data.binding] * 4 +
                               nir_intrinsic_base(intrin) * 16));
            break;
         }
         default:
            continue;
         }

         unsigned comps = nir_intrinsic_dest_components(intrin);

         nir_intrinsic_instr *load =
            nir_intrinsic_instr_create(nir, nir_intrinsic_load_ubo);
         load->num_components = comps;
         load->src[0] = nir_src_for_ssa(temp_ubo_name);
         load->src[1] = nir_src_for_ssa(offset);
         nir_ssa_dest_init(&load->instr, &load->dest, comps, 32, NULL);
         nir_builder_instr_insert(&b, &load->instr);
         nir_ssa_def_rewrite_uses(&intrin->dest.ssa,
                                  nir_src_for_ssa(&load->dest.ssa));
         nir_instr_remove(instr);
      }
   }

   nir_validate_shader(nir, "before remapping");

   /* Place the new params at the front of constant buffer 0. */
   if (num_system_values > 0) {
      nir->num_uniforms += num_system_values * sizeof(uint32_t);

      system_values = reralloc(mem_ctx, system_values, enum brw_param_builtin,
                               num_system_values);

      nir_foreach_block(block, impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *load = nir_instr_as_intrinsic(instr);

            if (load->intrinsic != nir_intrinsic_load_ubo)
               continue;

            b.cursor = nir_before_instr(instr);

            assert(load->src[0].is_ssa);

            if (load->src[0].ssa == temp_ubo_name) {
               nir_instr_rewrite_src(instr, &load->src[0],
                                     nir_src_for_ssa(nir_imm_int(&b, 0)));
            } else if (nir_src_is_const(load->src[0]) &&
                       nir_src_as_uint(load->src[0]) == 0) {
               nir_ssa_def *offset =
                  nir_iadd(&b, load->src[1].ssa,
                           nir_imm_int(&b, 4 * num_system_values));
               nir_instr_rewrite_src(instr, &load->src[1],
                                     nir_src_for_ssa(offset));
            }
         }
      }

      /* We need to fold the new iadds for brw_nir_analyze_ubo_ranges */
      nir_opt_constant_folding(nir);
   } else {
      ralloc_free(system_values);
      system_values = NULL;
   }

   nir_validate_shader(nir, "after remap");

   if (nir->info.stage != MESA_SHADER_COMPUTE)
      brw_nir_analyze_ubo_ranges(compiler, nir, NULL, prog_data->ubo_ranges);

   /* We don't use params[], but fs_visitor::nir_setup_uniforms() asserts
    * about it for compute shaders, so go ahead and make some fake ones
    * which the backend will dead code eliminate.
    */
   prog_data->nr_params = nir->num_uniforms / 4;
   prog_data->param = rzalloc_array(mem_ctx, uint32_t, prog_data->nr_params);

   /* System values and uniforms are stored in constant buffer 0, the
    * user-facing UBOs are indexed by one.  So if any constant buffer is
    * needed, the constant buffer 0 will be needed, so account for it.
    */
   unsigned num_cbufs = nir->info.num_ubos;
   if (num_cbufs || num_system_values || nir->num_uniforms)
      num_cbufs++;

   *out_system_values = system_values;
   *out_num_system_values = num_system_values;
   *out_num_cbufs = num_cbufs;
}

static void
iris_debug_recompile(struct iris_context *ice,
                     struct shader_info *info,
                     unsigned program_string_id,
                     const void *key)
{
   struct iris_screen *screen = (struct iris_screen *) ice->ctx.screen;
   const struct brw_compiler *c = screen->compiler;

   if (!info)
      return;

   c->shader_perf_log(&ice->dbg, "Recompiling %s shader for program %s: %s\n",
                      _mesa_shader_stage_to_string(info->stage),
                      info->name ? info->name : "(no identifier)",
                      info->label ? info->label : "");

   const void *old_key =
      iris_find_previous_compile(ice, info->stage, program_string_id);

   brw_debug_key_recompile(c, &ice->dbg, info->stage, old_key, key);
}


/**
 * Compile a vertex shader, and upload the assembly.
 */
static struct iris_compiled_shader *
iris_compile_vs(struct iris_context *ice,
                struct iris_uncompiled_shader *ish,
                const struct brw_vs_prog_key *key)
{
   struct iris_screen *screen = (struct iris_screen *)ice->ctx.screen;
   const struct brw_compiler *compiler = screen->compiler;
   const struct gen_device_info *devinfo = &screen->devinfo;
   void *mem_ctx = ralloc_context(NULL);
   struct brw_vs_prog_data *vs_prog_data =
      rzalloc(mem_ctx, struct brw_vs_prog_data);
   struct brw_vue_prog_data *vue_prog_data = &vs_prog_data->base;
   struct brw_stage_prog_data *prog_data = &vue_prog_data->base;
   enum brw_param_builtin *system_values;
   unsigned num_system_values;
   unsigned num_cbufs;

   nir_shader *nir = nir_shader_clone(mem_ctx, ish->nir);

   if (key->nr_userclip_plane_consts) {
      nir_function_impl *impl = nir_shader_get_entrypoint(nir);
      nir_lower_clip_vs(nir, (1 << key->nr_userclip_plane_consts) - 1, true);
      nir_lower_io_to_temporaries(nir, impl, true, false);
      nir_lower_global_vars_to_local(nir);
      nir_lower_vars_to_ssa(nir);
      nir_shader_gather_info(nir, impl);
   }

   if (nir->info.name && strncmp(nir->info.name, "ARB", 3) == 0)
      prog_data->use_alt_mode = true;

   iris_setup_uniforms(compiler, mem_ctx, nir, prog_data, &system_values,
                       &num_system_values, &num_cbufs);

   assign_common_binding_table_offsets(devinfo, nir, prog_data, 0,
                                       num_system_values, num_cbufs);

   brw_compute_vue_map(devinfo,
                       &vue_prog_data->vue_map, nir->info.outputs_written,
                       nir->info.separate_shader);

   /* Don't tell the backend about our clip plane constants, we've already
    * lowered them in NIR and we don't want it doing it again.
    */
   struct brw_vs_prog_key key_no_ucp = *key;
   key_no_ucp.nr_userclip_plane_consts = 0;

   char *error_str = NULL;
   const unsigned *program =
      brw_compile_vs(compiler, &ice->dbg, mem_ctx, &key_no_ucp, vs_prog_data,
                     nir, -1, &error_str);
   if (program == NULL) {
      dbg_printf("Failed to compile vertex shader: %s\n", error_str);
      ralloc_free(mem_ctx);
      return false;
   }

   if (ish->compiled_once) {
      iris_debug_recompile(ice, &nir->info, key->program_string_id, key);
   } else {
      ish->compiled_once = true;
   }

   uint32_t *so_decls =
      ice->vtbl.create_so_decl_list(&ish->stream_output,
                                    &vue_prog_data->vue_map);

   struct iris_compiled_shader *shader =
      iris_upload_shader(ice, IRIS_CACHE_VS, sizeof(*key), key, program,
                         prog_data, so_decls, system_values, num_system_values,
                         num_cbufs);

   ralloc_free(mem_ctx);
   return shader;
}

/**
 * Update the current vertex shader variant.
 *
 * Fill out the key, look in the cache, compile and bind if needed.
 */
static void
iris_update_compiled_vs(struct iris_context *ice)
{
   struct iris_uncompiled_shader *ish =
      ice->shaders.uncompiled[MESA_SHADER_VERTEX];
   struct iris_screen *screen = (struct iris_screen *)ice->ctx.screen;
   const struct gen_device_info *devinfo = &screen->devinfo;

   struct brw_vs_prog_key key = { KEY_INIT(devinfo->gen) };
   ice->vtbl.populate_vs_key(ice, &ish->nir->info, &key);

   struct iris_compiled_shader *old = ice->shaders.prog[IRIS_CACHE_VS];
   struct iris_compiled_shader *shader =
      iris_find_cached_shader(ice, IRIS_CACHE_VS, sizeof(key), &key);

   if (!shader)
      shader = iris_compile_vs(ice, ish, &key);

   if (old != shader) {
      ice->shaders.prog[IRIS_CACHE_VS] = shader;
      ice->state.dirty |= IRIS_DIRTY_VS |
                          IRIS_DIRTY_BINDINGS_VS |
                          IRIS_DIRTY_CONSTANTS_VS |
                          IRIS_DIRTY_VF_SGVS;
      const struct brw_vs_prog_data *vs_prog_data =
            (void *) shader->prog_data;
      const bool uses_draw_params = vs_prog_data->uses_firstvertex ||
                                    vs_prog_data->uses_baseinstance;
      const bool uses_derived_draw_params = vs_prog_data->uses_drawid ||
                                            vs_prog_data->uses_is_indexed_draw;
      const bool needs_sgvs_element = uses_draw_params ||
                                      vs_prog_data->uses_instanceid ||
                                      vs_prog_data->uses_vertexid;
      bool needs_edge_flag = false;
      nir_foreach_variable(var, &ish->nir->inputs) {
         if (var->data.location == VERT_ATTRIB_EDGEFLAG)
            needs_edge_flag = true;
      }

      if (ice->state.vs_uses_draw_params != uses_draw_params ||
          ice->state.vs_uses_derived_draw_params != uses_derived_draw_params ||
          ice->state.vs_needs_edge_flag != needs_edge_flag) {
         ice->state.dirty |= IRIS_DIRTY_VERTEX_BUFFERS |
                             IRIS_DIRTY_VERTEX_ELEMENTS;
      }
      ice->state.vs_uses_draw_params = uses_draw_params;
      ice->state.vs_uses_derived_draw_params = uses_derived_draw_params;
      ice->state.vs_needs_sgvs_element = needs_sgvs_element;
      ice->state.vs_needs_edge_flag = needs_edge_flag;
   }
}

/**
 * Get the shader_info for a given stage, or NULL if the stage is disabled.
 */
const struct shader_info *
iris_get_shader_info(const struct iris_context *ice, gl_shader_stage stage)
{
   const struct iris_uncompiled_shader *ish = ice->shaders.uncompiled[stage];

   if (!ish)
      return NULL;

   const nir_shader *nir = ish->nir;
   return &nir->info;
}

/**
 * Get the union of TCS output and TES input slots.
 *
 * TCS and TES need to agree on a common URB entry layout.  In particular,
 * the data for all patch vertices is stored in a single URB entry (unlike
 * GS which has one entry per input vertex).  This means that per-vertex
 * array indexing needs a stride.
 *
 * SSO requires locations to match, but doesn't require the number of
 * outputs/inputs to match (in fact, the TCS often has extra outputs).
 * So, we need to take the extra step of unifying these on the fly.
 */
static void
get_unified_tess_slots(const struct iris_context *ice,
                       uint64_t *per_vertex_slots,
                       uint32_t *per_patch_slots)
{
   const struct shader_info *tcs =
      iris_get_shader_info(ice, MESA_SHADER_TESS_CTRL);
   const struct shader_info *tes =
      iris_get_shader_info(ice, MESA_SHADER_TESS_EVAL);

   *per_vertex_slots = tes->inputs_read;
   *per_patch_slots = tes->patch_inputs_read;

   if (tcs) {
      *per_vertex_slots |= tcs->outputs_written;
      *per_patch_slots |= tcs->patch_outputs_written;
   }
}

/**
 * Compile a tessellation control shader, and upload the assembly.
 */
static struct iris_compiled_shader *
iris_compile_tcs(struct iris_context *ice,
                 struct iris_uncompiled_shader *ish,
                 const struct brw_tcs_prog_key *key)
{
   struct iris_screen *screen = (struct iris_screen *)ice->ctx.screen;
   const struct brw_compiler *compiler = screen->compiler;
   const struct nir_shader_compiler_options *options =
      compiler->glsl_compiler_options[MESA_SHADER_TESS_CTRL].NirOptions;
   const struct gen_device_info *devinfo = &screen->devinfo;
   void *mem_ctx = ralloc_context(NULL);
   struct brw_tcs_prog_data *tcs_prog_data =
      rzalloc(mem_ctx, struct brw_tcs_prog_data);
   struct brw_vue_prog_data *vue_prog_data = &tcs_prog_data->base;
   struct brw_stage_prog_data *prog_data = &vue_prog_data->base;
   enum brw_param_builtin *system_values = NULL;
   unsigned num_system_values = 0;
   unsigned num_cbufs = 0;

   nir_shader *nir;

   if (ish) {
      nir = nir_shader_clone(mem_ctx, ish->nir);

      iris_setup_uniforms(compiler, mem_ctx, nir, prog_data, &system_values,
                          &num_system_values, &num_cbufs);
      assign_common_binding_table_offsets(devinfo, nir, prog_data, 0,
                                          num_system_values, num_cbufs);
   } else {
      nir = brw_nir_create_passthrough_tcs(mem_ctx, compiler, options, key);

      /* Reserve space for passing the default tess levels as constants. */
      num_system_values = 8;
      system_values =
         rzalloc_array(mem_ctx, enum brw_param_builtin, num_system_values);
      prog_data->param = rzalloc_array(mem_ctx, uint32_t, num_system_values);
      prog_data->nr_params = num_system_values;

      if (key->tes_primitive_mode == GL_QUADS) {
         for (int i = 0; i < 4; i++)
            system_values[7 - i] = BRW_PARAM_BUILTIN_TESS_LEVEL_OUTER_X + i;

         system_values[3] = BRW_PARAM_BUILTIN_TESS_LEVEL_INNER_X;
         system_values[2] = BRW_PARAM_BUILTIN_TESS_LEVEL_INNER_Y;
      } else if (key->tes_primitive_mode == GL_TRIANGLES) {
         for (int i = 0; i < 3; i++)
            system_values[7 - i] = BRW_PARAM_BUILTIN_TESS_LEVEL_OUTER_X + i;

         system_values[4] = BRW_PARAM_BUILTIN_TESS_LEVEL_INNER_X;
      } else {
         assert(key->tes_primitive_mode == GL_ISOLINES);
         system_values[7] = BRW_PARAM_BUILTIN_TESS_LEVEL_OUTER_Y;
         system_values[6] = BRW_PARAM_BUILTIN_TESS_LEVEL_OUTER_X;
      }

      prog_data->ubo_ranges[0].length = 1;
   }

   char *error_str = NULL;
   const unsigned *program =
      brw_compile_tcs(compiler, &ice->dbg, mem_ctx, key, tcs_prog_data, nir,
                      -1, &error_str);
   if (program == NULL) {
      dbg_printf("Failed to compile control shader: %s\n", error_str);
      ralloc_free(mem_ctx);
      return false;
   }

   if (ish) {
      if (ish->compiled_once) {
         iris_debug_recompile(ice, &nir->info, key->program_string_id, key);
      } else {
         ish->compiled_once = true;
      }
   }

   struct iris_compiled_shader *shader =
      iris_upload_shader(ice, IRIS_CACHE_TCS, sizeof(*key), key, program,
                         prog_data, NULL, system_values, num_system_values,
                         num_cbufs);

   ralloc_free(mem_ctx);
   return shader;
}

/**
 * Update the current tessellation control shader variant.
 *
 * Fill out the key, look in the cache, compile and bind if needed.
 */
static void
iris_update_compiled_tcs(struct iris_context *ice)
{
   struct iris_uncompiled_shader *tcs =
      ice->shaders.uncompiled[MESA_SHADER_TESS_CTRL];
   struct iris_screen *screen = (struct iris_screen *)ice->ctx.screen;
   const struct gen_device_info *devinfo = &screen->devinfo;

   const struct shader_info *tes_info =
      iris_get_shader_info(ice, MESA_SHADER_TESS_EVAL);
   struct brw_tcs_prog_key key = {
      KEY_INIT_NO_ID(devinfo->gen),
      .program_string_id = tcs ? tcs->program_id : 0,
      .tes_primitive_mode = tes_info->tess.primitive_mode,
      .input_vertices = ice->state.vertices_per_patch,
   };
   get_unified_tess_slots(ice, &key.outputs_written,
                          &key.patch_outputs_written);
   ice->vtbl.populate_tcs_key(ice, &key);

   struct iris_compiled_shader *old = ice->shaders.prog[IRIS_CACHE_TCS];
   struct iris_compiled_shader *shader =
      iris_find_cached_shader(ice, IRIS_CACHE_TCS, sizeof(key), &key);

   if (!shader)
      shader = iris_compile_tcs(ice, tcs, &key);

   if (old != shader) {
      ice->shaders.prog[IRIS_CACHE_TCS] = shader;
      ice->state.dirty |= IRIS_DIRTY_TCS |
                          IRIS_DIRTY_BINDINGS_TCS |
                          IRIS_DIRTY_CONSTANTS_TCS;
   }
}

/**
 * Compile a tessellation evaluation shader, and upload the assembly.
 */
static struct iris_compiled_shader *
iris_compile_tes(struct iris_context *ice,
                 struct iris_uncompiled_shader *ish,
                 const struct brw_tes_prog_key *key)
{
   struct iris_screen *screen = (struct iris_screen *)ice->ctx.screen;
   const struct brw_compiler *compiler = screen->compiler;
   const struct gen_device_info *devinfo = &screen->devinfo;
   void *mem_ctx = ralloc_context(NULL);
   struct brw_tes_prog_data *tes_prog_data =
      rzalloc(mem_ctx, struct brw_tes_prog_data);
   struct brw_vue_prog_data *vue_prog_data = &tes_prog_data->base;
   struct brw_stage_prog_data *prog_data = &vue_prog_data->base;
   enum brw_param_builtin *system_values;
   unsigned num_system_values;
   unsigned num_cbufs;

   nir_shader *nir = nir_shader_clone(mem_ctx, ish->nir);

   iris_setup_uniforms(compiler, mem_ctx, nir, prog_data, &system_values,
                       &num_system_values, &num_cbufs);

   assign_common_binding_table_offsets(devinfo, nir, prog_data, 0,
                                       num_system_values, num_cbufs);

   struct brw_vue_map input_vue_map;
   brw_compute_tess_vue_map(&input_vue_map, key->inputs_read,
                            key->patch_inputs_read);

   char *error_str = NULL;
   const unsigned *program =
      brw_compile_tes(compiler, &ice->dbg, mem_ctx, key, &input_vue_map,
                      tes_prog_data, nir, NULL, -1, &error_str);
   if (program == NULL) {
      dbg_printf("Failed to compile evaluation shader: %s\n", error_str);
      ralloc_free(mem_ctx);
      return false;
   }

   if (ish->compiled_once) {
      iris_debug_recompile(ice, &nir->info, key->program_string_id, key);
   } else {
      ish->compiled_once = true;
   }

   uint32_t *so_decls =
      ice->vtbl.create_so_decl_list(&ish->stream_output,
                                    &vue_prog_data->vue_map);


   struct iris_compiled_shader *shader =
      iris_upload_shader(ice, IRIS_CACHE_TES, sizeof(*key), key, program,
                         prog_data, so_decls, system_values, num_system_values,
                         num_cbufs);

   ralloc_free(mem_ctx);
   return shader;
}

/**
 * Update the current tessellation evaluation shader variant.
 *
 * Fill out the key, look in the cache, compile and bind if needed.
 */
static void
iris_update_compiled_tes(struct iris_context *ice)
{
   struct iris_uncompiled_shader *ish =
      ice->shaders.uncompiled[MESA_SHADER_TESS_EVAL];
   struct iris_screen *screen = (struct iris_screen *)ice->ctx.screen;
   const struct gen_device_info *devinfo = &screen->devinfo;

   struct brw_tes_prog_key key = { KEY_INIT(devinfo->gen) };
   get_unified_tess_slots(ice, &key.inputs_read, &key.patch_inputs_read);
   ice->vtbl.populate_tes_key(ice, &key);

   struct iris_compiled_shader *old = ice->shaders.prog[IRIS_CACHE_TES];
   struct iris_compiled_shader *shader =
      iris_find_cached_shader(ice, IRIS_CACHE_TES, sizeof(key), &key);

   if (!shader)
      shader = iris_compile_tes(ice, ish, &key);

   if (old != shader) {
      ice->shaders.prog[IRIS_CACHE_TES] = shader;
      ice->state.dirty |= IRIS_DIRTY_TES |
                          IRIS_DIRTY_BINDINGS_TES |
                          IRIS_DIRTY_CONSTANTS_TES;
   }

   /* TODO: Could compare and avoid flagging this. */
   const struct shader_info *tes_info = &ish->nir->info;
   if (tes_info->system_values_read & (1ull << SYSTEM_VALUE_VERTICES_IN)) {
      ice->state.dirty |= IRIS_DIRTY_CONSTANTS_TES;
      ice->state.shaders[MESA_SHADER_TESS_EVAL].cbuf0_needs_upload = true;
   }
}

/**
 * Compile a geometry shader, and upload the assembly.
 */
static struct iris_compiled_shader *
iris_compile_gs(struct iris_context *ice,
                struct iris_uncompiled_shader *ish,
                const struct brw_gs_prog_key *key)
{
   struct iris_screen *screen = (struct iris_screen *)ice->ctx.screen;
   const struct brw_compiler *compiler = screen->compiler;
   const struct gen_device_info *devinfo = &screen->devinfo;
   void *mem_ctx = ralloc_context(NULL);
   struct brw_gs_prog_data *gs_prog_data =
      rzalloc(mem_ctx, struct brw_gs_prog_data);
   struct brw_vue_prog_data *vue_prog_data = &gs_prog_data->base;
   struct brw_stage_prog_data *prog_data = &vue_prog_data->base;
   enum brw_param_builtin *system_values;
   unsigned num_system_values;
   unsigned num_cbufs;

   nir_shader *nir = nir_shader_clone(mem_ctx, ish->nir);

   iris_setup_uniforms(compiler, mem_ctx, nir, prog_data, &system_values,
                       &num_system_values, &num_cbufs);

   assign_common_binding_table_offsets(devinfo, nir, prog_data, 0,
                                       num_system_values, num_cbufs);

   brw_compute_vue_map(devinfo,
                       &vue_prog_data->vue_map, nir->info.outputs_written,
                       nir->info.separate_shader);

   char *error_str = NULL;
   const unsigned *program =
      brw_compile_gs(compiler, &ice->dbg, mem_ctx, key, gs_prog_data, nir,
                     NULL, -1, &error_str);
   if (program == NULL) {
      dbg_printf("Failed to compile geometry shader: %s\n", error_str);
      ralloc_free(mem_ctx);
      return false;
   }

   if (ish->compiled_once) {
      iris_debug_recompile(ice, &nir->info, key->program_string_id, key);
   } else {
      ish->compiled_once = true;
   }

   uint32_t *so_decls =
      ice->vtbl.create_so_decl_list(&ish->stream_output,
                                    &vue_prog_data->vue_map);

   struct iris_compiled_shader *shader =
      iris_upload_shader(ice, IRIS_CACHE_GS, sizeof(*key), key, program,
                         prog_data, so_decls, system_values, num_system_values,
                         num_cbufs);

   ralloc_free(mem_ctx);
   return shader;
}

/**
 * Update the current geometry shader variant.
 *
 * Fill out the key, look in the cache, compile and bind if needed.
 */
static void
iris_update_compiled_gs(struct iris_context *ice)
{
   struct iris_uncompiled_shader *ish =
      ice->shaders.uncompiled[MESA_SHADER_GEOMETRY];
   struct iris_compiled_shader *old = ice->shaders.prog[IRIS_CACHE_GS];
   struct iris_compiled_shader *shader = NULL;

   if (ish) {
      struct iris_screen *screen = (struct iris_screen *)ice->ctx.screen;
      const struct gen_device_info *devinfo = &screen->devinfo;
      struct brw_gs_prog_key key = { KEY_INIT(devinfo->gen) };
      ice->vtbl.populate_gs_key(ice, &key);

      shader =
         iris_find_cached_shader(ice, IRIS_CACHE_GS, sizeof(key), &key);

      if (!shader)
         shader = iris_compile_gs(ice, ish, &key);
   }

   if (old != shader) {
      ice->shaders.prog[IRIS_CACHE_GS] = shader;
      ice->state.dirty |= IRIS_DIRTY_GS |
                          IRIS_DIRTY_BINDINGS_GS |
                          IRIS_DIRTY_CONSTANTS_GS;
   }
}

/**
 * Compile a fragment (pixel) shader, and upload the assembly.
 */
static struct iris_compiled_shader *
iris_compile_fs(struct iris_context *ice,
                struct iris_uncompiled_shader *ish,
                const struct brw_wm_prog_key *key,
                struct brw_vue_map *vue_map)
{
   struct iris_screen *screen = (struct iris_screen *)ice->ctx.screen;
   const struct brw_compiler *compiler = screen->compiler;
   const struct gen_device_info *devinfo = &screen->devinfo;
   void *mem_ctx = ralloc_context(NULL);
   struct brw_wm_prog_data *fs_prog_data =
      rzalloc(mem_ctx, struct brw_wm_prog_data);
   struct brw_stage_prog_data *prog_data = &fs_prog_data->base;
   enum brw_param_builtin *system_values;
   unsigned num_system_values;
   unsigned num_cbufs;

   nir_shader *nir = nir_shader_clone(mem_ctx, ish->nir);

   if (nir->info.name && strncmp(nir->info.name, "ARB", 3) == 0)
      prog_data->use_alt_mode = true;

   iris_setup_uniforms(compiler, mem_ctx, nir, prog_data, &system_values,
                       &num_system_values, &num_cbufs);

   assign_common_binding_table_offsets(devinfo, nir, prog_data,
                                       MAX2(key->nr_color_regions, 1),
                                       num_system_values, num_cbufs);
   char *error_str = NULL;
   const unsigned *program =
      brw_compile_fs(compiler, &ice->dbg, mem_ctx, key, fs_prog_data,
                     nir, NULL, -1, -1, -1, true, false, vue_map, &error_str);
   if (program == NULL) {
      dbg_printf("Failed to compile fragment shader: %s\n", error_str);
      ralloc_free(mem_ctx);
      return false;
   }

   if (ish->compiled_once) {
      iris_debug_recompile(ice, &nir->info, key->program_string_id, key);
   } else {
      ish->compiled_once = true;
   }

   struct iris_compiled_shader *shader =
      iris_upload_shader(ice, IRIS_CACHE_FS, sizeof(*key), key, program,
                         prog_data, NULL, system_values, num_system_values,
                         num_cbufs);

   ralloc_free(mem_ctx);
   return shader;
}

/**
 * Update the current fragment shader variant.
 *
 * Fill out the key, look in the cache, compile and bind if needed.
 */
static void
iris_update_compiled_fs(struct iris_context *ice)
{
   struct iris_uncompiled_shader *ish =
      ice->shaders.uncompiled[MESA_SHADER_FRAGMENT];
      struct iris_screen *screen = (struct iris_screen *)ice->ctx.screen;
      const struct gen_device_info *devinfo = &screen->devinfo;
   struct brw_wm_prog_key key = { KEY_INIT(devinfo->gen) };
   ice->vtbl.populate_fs_key(ice, &key);

   if (ish->nos & (1ull << IRIS_NOS_LAST_VUE_MAP))
      key.input_slots_valid = ice->shaders.last_vue_map->slots_valid;

   struct iris_compiled_shader *old = ice->shaders.prog[IRIS_CACHE_FS];
   struct iris_compiled_shader *shader =
      iris_find_cached_shader(ice, IRIS_CACHE_FS, sizeof(key), &key);

   if (!shader)
      shader = iris_compile_fs(ice, ish, &key, ice->shaders.last_vue_map);

   if (old != shader) {
      // XXX: only need to flag CLIP if barycentric has NONPERSPECTIVE
      // toggles.  might be able to avoid flagging SBE too.
      ice->shaders.prog[IRIS_CACHE_FS] = shader;
      ice->state.dirty |= IRIS_DIRTY_FS |
                          IRIS_DIRTY_BINDINGS_FS |
                          IRIS_DIRTY_CONSTANTS_FS |
                          IRIS_DIRTY_WM |
                          IRIS_DIRTY_CLIP |
                          IRIS_DIRTY_SBE;
   }
}

/**
 * Get the compiled shader for the last enabled geometry stage.
 *
 * This stage is the one which will feed stream output and the rasterizer.
 */
static gl_shader_stage
last_vue_stage(struct iris_context *ice)
{
   if (ice->shaders.prog[MESA_SHADER_GEOMETRY])
      return MESA_SHADER_GEOMETRY;

   if (ice->shaders.prog[MESA_SHADER_TESS_EVAL])
      return MESA_SHADER_TESS_EVAL;

   return MESA_SHADER_VERTEX;
}

/**
 * Update the last enabled stage's VUE map.
 *
 * When the shader feeding the rasterizer's output interface changes, we
 * need to re-emit various packets.
 */
static void
update_last_vue_map(struct iris_context *ice,
                    struct brw_stage_prog_data *prog_data)
{
   struct brw_vue_prog_data *vue_prog_data = (void *) prog_data;
   struct brw_vue_map *vue_map = &vue_prog_data->vue_map;
   struct brw_vue_map *old_map = ice->shaders.last_vue_map;
   const uint64_t changed_slots =
      (old_map ? old_map->slots_valid : 0ull) ^ vue_map->slots_valid;

   if (changed_slots & VARYING_BIT_VIEWPORT) {
      // XXX: could use ctx->Const.MaxViewports for old API efficiency
      ice->state.num_viewports =
         (vue_map->slots_valid & VARYING_BIT_VIEWPORT) ? IRIS_MAX_VIEWPORTS : 1;
      ice->state.dirty |= IRIS_DIRTY_CLIP |
                          IRIS_DIRTY_SF_CL_VIEWPORT |
                          IRIS_DIRTY_CC_VIEWPORT |
                          IRIS_DIRTY_SCISSOR_RECT |
                          IRIS_DIRTY_UNCOMPILED_FS |
                          ice->state.dirty_for_nos[IRIS_NOS_LAST_VUE_MAP];
      // XXX: CC_VIEWPORT?
   }

   if (changed_slots || (old_map && old_map->separate != vue_map->separate)) {
      ice->state.dirty |= IRIS_DIRTY_SBE;
   }

   ice->shaders.last_vue_map = &vue_prog_data->vue_map;
}

/**
 * Get the prog_data for a given stage, or NULL if the stage is disabled.
 */
static struct brw_vue_prog_data *
get_vue_prog_data(struct iris_context *ice, gl_shader_stage stage)
{
   if (!ice->shaders.prog[stage])
      return NULL;

   return (void *) ice->shaders.prog[stage]->prog_data;
}

// XXX: iris_compiled_shaders are space-leaking :(
// XXX: do remember to unbind them if deleting them.

/**
 * Update the current shader variants for the given state.
 *
 * This should be called on every draw call to ensure that the correct
 * shaders are bound.  It will also flag any dirty state triggered by
 * swapping out those shaders.
 */
void
iris_update_compiled_shaders(struct iris_context *ice)
{
   const uint64_t dirty = ice->state.dirty;

   struct brw_vue_prog_data *old_prog_datas[4];
   if (!(dirty & IRIS_DIRTY_URB)) {
      for (int i = MESA_SHADER_VERTEX; i <= MESA_SHADER_GEOMETRY; i++)
         old_prog_datas[i] = get_vue_prog_data(ice, i);
   }

   if (dirty & (IRIS_DIRTY_UNCOMPILED_TCS | IRIS_DIRTY_UNCOMPILED_TES)) {
       struct iris_uncompiled_shader *tes =
          ice->shaders.uncompiled[MESA_SHADER_TESS_EVAL];
       if (tes) {
          iris_update_compiled_tcs(ice);
          iris_update_compiled_tes(ice);
       } else {
          ice->shaders.prog[IRIS_CACHE_TCS] = NULL;
          ice->shaders.prog[IRIS_CACHE_TES] = NULL;
          ice->state.dirty |=
             IRIS_DIRTY_TCS | IRIS_DIRTY_TES |
             IRIS_DIRTY_BINDINGS_TCS | IRIS_DIRTY_BINDINGS_TES |
             IRIS_DIRTY_CONSTANTS_TCS | IRIS_DIRTY_CONSTANTS_TES;
       }
   }

   if (dirty & IRIS_DIRTY_UNCOMPILED_VS)
      iris_update_compiled_vs(ice);
   if (dirty & IRIS_DIRTY_UNCOMPILED_GS)
      iris_update_compiled_gs(ice);

   if (dirty & (IRIS_DIRTY_UNCOMPILED_GS | IRIS_DIRTY_UNCOMPILED_TES)) {
      const struct iris_compiled_shader *gs =
         ice->shaders.prog[MESA_SHADER_GEOMETRY];
      const struct iris_compiled_shader *tes =
         ice->shaders.prog[MESA_SHADER_TESS_EVAL];

      bool points_or_lines = false;

      if (gs) {
         const struct brw_gs_prog_data *gs_prog_data = (void *) gs->prog_data;
         points_or_lines =
            gs_prog_data->output_topology == _3DPRIM_POINTLIST ||
            gs_prog_data->output_topology == _3DPRIM_LINESTRIP;
      } else if (tes) {
         const struct brw_tes_prog_data *tes_data = (void *) tes->prog_data;
         points_or_lines =
            tes_data->output_topology == BRW_TESS_OUTPUT_TOPOLOGY_LINE ||
            tes_data->output_topology == BRW_TESS_OUTPUT_TOPOLOGY_POINT;
      }

      if (ice->shaders.output_topology_is_points_or_lines != points_or_lines) {
         /* Outbound to XY Clip enables */
         ice->shaders.output_topology_is_points_or_lines = points_or_lines;
         ice->state.dirty |= IRIS_DIRTY_CLIP;
      }
   }

   gl_shader_stage last_stage = last_vue_stage(ice);
   struct iris_compiled_shader *shader = ice->shaders.prog[last_stage];
   struct iris_uncompiled_shader *ish = ice->shaders.uncompiled[last_stage];
   update_last_vue_map(ice, shader->prog_data);
   if (ice->state.streamout != shader->streamout) {
      ice->state.streamout = shader->streamout;
      ice->state.dirty |= IRIS_DIRTY_SO_DECL_LIST | IRIS_DIRTY_STREAMOUT;
   }

   if (ice->state.streamout_active) {
      for (int i = 0; i < PIPE_MAX_SO_BUFFERS; i++) {
         struct iris_stream_output_target *so =
            (void *) ice->state.so_target[i];
         if (so)
            so->stride = ish->stream_output.stride[i];
      }
   }

   if (dirty & IRIS_DIRTY_UNCOMPILED_FS)
      iris_update_compiled_fs(ice);

   /* Changing shader interfaces may require a URB configuration. */
   if (!(dirty & IRIS_DIRTY_URB)) {
      for (int i = MESA_SHADER_VERTEX; i <= MESA_SHADER_GEOMETRY; i++) {
         struct brw_vue_prog_data *old = old_prog_datas[i];
         struct brw_vue_prog_data *new = get_vue_prog_data(ice, i);
         if (!!old != !!new ||
             (new && new->urb_entry_size != old->urb_entry_size)) {
            ice->state.dirty |= IRIS_DIRTY_URB;
            break;
         }
      }
   }
}

static struct iris_compiled_shader *
iris_compile_cs(struct iris_context *ice,
                struct iris_uncompiled_shader *ish,
                const struct brw_cs_prog_key *key)
{
   struct iris_screen *screen = (struct iris_screen *)ice->ctx.screen;
   const struct brw_compiler *compiler = screen->compiler;
   const struct gen_device_info *devinfo = &screen->devinfo;
   void *mem_ctx = ralloc_context(NULL);
   struct brw_cs_prog_data *cs_prog_data =
      rzalloc(mem_ctx, struct brw_cs_prog_data);
   struct brw_stage_prog_data *prog_data = &cs_prog_data->base;
   enum brw_param_builtin *system_values;
   unsigned num_system_values;
   unsigned num_cbufs;

   nir_shader *nir = nir_shader_clone(mem_ctx, ish->nir);

   cs_prog_data->binding_table.work_groups_start = 0;

   prog_data->total_shared = nir->info.cs.shared_size;

   iris_setup_uniforms(compiler, mem_ctx, nir, prog_data, &system_values,
                       &num_system_values, &num_cbufs);

   assign_common_binding_table_offsets(devinfo, nir, prog_data, 1,
                                       num_system_values, num_cbufs);

   char *error_str = NULL;
   const unsigned *program =
      brw_compile_cs(compiler, &ice->dbg, mem_ctx, key, cs_prog_data,
                     nir, -1, &error_str);
   if (program == NULL) {
      dbg_printf("Failed to compile compute shader: %s\n", error_str);
      ralloc_free(mem_ctx);
      return false;
   }

   if (ish->compiled_once) {
      iris_debug_recompile(ice, &nir->info, key->program_string_id, key);
   } else {
      ish->compiled_once = true;
   }

   struct iris_compiled_shader *shader =
      iris_upload_shader(ice, IRIS_CACHE_CS, sizeof(*key), key, program,
                         prog_data, NULL, system_values, num_system_values,
                         num_cbufs);

   ralloc_free(mem_ctx);
   return shader;
}

void
iris_update_compiled_compute_shader(struct iris_context *ice)
{
   struct iris_uncompiled_shader *ish =
      ice->shaders.uncompiled[MESA_SHADER_COMPUTE];

   struct iris_screen *screen = (struct iris_screen *)ice->ctx.screen;
   const struct gen_device_info *devinfo = &screen->devinfo;
   struct brw_cs_prog_key key = { KEY_INIT(devinfo->gen) };
   ice->vtbl.populate_cs_key(ice, &key);

   struct iris_compiled_shader *old = ice->shaders.prog[IRIS_CACHE_CS];
   struct iris_compiled_shader *shader =
      iris_find_cached_shader(ice, IRIS_CACHE_CS, sizeof(key), &key);

   if (!shader)
      shader = iris_compile_cs(ice, ish, &key);

   if (old != shader) {
      ice->shaders.prog[IRIS_CACHE_CS] = shader;
      ice->state.dirty |= IRIS_DIRTY_CS |
                          IRIS_DIRTY_BINDINGS_CS |
                          IRIS_DIRTY_CONSTANTS_CS;
   }
}

void
iris_fill_cs_push_const_buffer(struct brw_cs_prog_data *cs_prog_data,
                               uint32_t *dst)
{
   assert(cs_prog_data->push.total.size > 0);
   assert(cs_prog_data->push.cross_thread.size == 0);
   assert(cs_prog_data->push.per_thread.dwords == 1);
   assert(cs_prog_data->base.param[0] == BRW_PARAM_BUILTIN_SUBGROUP_ID);
   for (unsigned t = 0; t < cs_prog_data->threads; t++)
      dst[8 * t] = t;
}

/**
 * Allocate scratch BOs as needed for the given per-thread size and stage.
 */
struct iris_bo *
iris_get_scratch_space(struct iris_context *ice,
                       unsigned per_thread_scratch,
                       gl_shader_stage stage)
{
   struct iris_screen *screen = (struct iris_screen *)ice->ctx.screen;
   struct iris_bufmgr *bufmgr = screen->bufmgr;
   const struct gen_device_info *devinfo = &screen->devinfo;

   unsigned encoded_size = ffs(per_thread_scratch) - 11;
   assert(encoded_size < (1 << 16));

   struct iris_bo **bop = &ice->shaders.scratch_bos[encoded_size][stage];

   /* The documentation for 3DSTATE_PS "Scratch Space Base Pointer" says:
    *
    *    "Scratch Space per slice is computed based on 4 sub-slices.  SW
    *     must allocate scratch space enough so that each slice has 4
    *     slices allowed."
    *
    * According to the other driver team, this applies to compute shaders
    * as well.  This is not currently documented at all.
    *
    * This hack is no longer necessary on Gen11+.
    */
   unsigned subslice_total = screen->subslice_total;
   if (devinfo->gen < 11)
      subslice_total = 4 * devinfo->num_slices;
   assert(subslice_total >= screen->subslice_total);

   if (!*bop) {
      unsigned scratch_ids_per_subslice = devinfo->max_cs_threads;
      uint32_t max_threads[] = {
         [MESA_SHADER_VERTEX]    = devinfo->max_vs_threads,
         [MESA_SHADER_TESS_CTRL] = devinfo->max_tcs_threads,
         [MESA_SHADER_TESS_EVAL] = devinfo->max_tes_threads,
         [MESA_SHADER_GEOMETRY]  = devinfo->max_gs_threads,
         [MESA_SHADER_FRAGMENT]  = devinfo->max_wm_threads,
         [MESA_SHADER_COMPUTE]   = scratch_ids_per_subslice * subslice_total,
      };

      uint32_t size = per_thread_scratch * max_threads[stage];

      *bop = iris_bo_alloc(bufmgr, "scratch", size, IRIS_MEMZONE_SHADER);
   }

   return *bop;
}

/* ------------------------------------------------------------------- */

/**
 * The pipe->create_[stage]_state() driver hooks.
 *
 * Performs basic NIR preprocessing, records any state dependencies, and
 * returns an iris_uncompiled_shader as the Gallium CSO.
 *
 * Actual shader compilation to assembly happens later, at first use.
 */
static void *
iris_create_uncompiled_shader(struct pipe_context *ctx,
                              nir_shader *nir,
                              const struct pipe_stream_output_info *so_info)
{
   struct iris_screen *screen = (struct iris_screen *)ctx->screen;
   const struct gen_device_info *devinfo = &screen->devinfo;

   struct iris_uncompiled_shader *ish =
      calloc(1, sizeof(struct iris_uncompiled_shader));
   if (!ish)
      return NULL;

   nir = brw_preprocess_nir(screen->compiler, nir, NULL);

   NIR_PASS_V(nir, brw_nir_lower_image_load_store, devinfo);
   NIR_PASS_V(nir, iris_lower_storage_image_derefs);

   ish->program_id = get_new_program_id(screen);
   ish->nir = nir;
   if (so_info) {
      memcpy(&ish->stream_output, so_info, sizeof(*so_info));
      update_so_info(&ish->stream_output, nir->info.outputs_written);
   }

   return ish;
}

static struct iris_uncompiled_shader *
iris_create_shader_state(struct pipe_context *ctx,
                         const struct pipe_shader_state *state)
{
   struct nir_shader *nir;

   if (state->type == PIPE_SHADER_IR_TGSI)
      nir = tgsi_to_nir(state->tokens, ctx->screen);
   else
      nir = state->ir.nir;

   return iris_create_uncompiled_shader(ctx, nir, &state->stream_output);
}

static void *
iris_create_vs_state(struct pipe_context *ctx,
                     const struct pipe_shader_state *state)
{
   struct iris_context *ice = (void *) ctx;
   struct iris_screen *screen = (void *) ctx->screen;
   struct iris_uncompiled_shader *ish = iris_create_shader_state(ctx, state);

   /* User clip planes */
   if (ish->nir->info.clip_distance_array_size == 0)
      ish->nos |= (1ull << IRIS_NOS_RASTERIZER);

   if (screen->precompile) {
      const struct gen_device_info *devinfo = &screen->devinfo;
      struct brw_vs_prog_key key = { KEY_INIT(devinfo->gen) };

      iris_compile_vs(ice, ish, &key);
   }

   return ish;
}

static void *
iris_create_tcs_state(struct pipe_context *ctx,
                      const struct pipe_shader_state *state)
{
   struct iris_context *ice = (void *) ctx;
   struct iris_screen *screen = (void *) ctx->screen;
   struct iris_uncompiled_shader *ish = iris_create_shader_state(ctx, state);
   struct shader_info *info = &ish->nir->info;

   // XXX: NOS?

   if (screen->precompile) {
      const unsigned _GL_TRIANGLES = 0x0004;
      const struct gen_device_info *devinfo = &screen->devinfo;
      struct brw_tcs_prog_key key = {
         KEY_INIT(devinfo->gen),
         // XXX: make sure the linker fills this out from the TES...
         .tes_primitive_mode =
            info->tess.primitive_mode ? info->tess.primitive_mode
                                      : _GL_TRIANGLES,
         .outputs_written = info->outputs_written,
         .patch_outputs_written = info->patch_outputs_written,
      };

      iris_compile_tcs(ice, ish, &key);
   }

   return ish;
}

static void *
iris_create_tes_state(struct pipe_context *ctx,
                     const struct pipe_shader_state *state)
{
   struct iris_context *ice = (void *) ctx;
   struct iris_screen *screen = (void *) ctx->screen;
   struct iris_uncompiled_shader *ish = iris_create_shader_state(ctx, state);
   struct shader_info *info = &ish->nir->info;

   // XXX: NOS?

   if (screen->precompile) {
      const struct gen_device_info *devinfo = &screen->devinfo;
      struct brw_tes_prog_key key = {
         KEY_INIT(devinfo->gen),
         // XXX: not ideal, need TCS output/TES input unification
         .inputs_read = info->inputs_read,
         .patch_inputs_read = info->patch_inputs_read,
      };

      iris_compile_tes(ice, ish, &key);
   }

   return ish;
}

static void *
iris_create_gs_state(struct pipe_context *ctx,
                     const struct pipe_shader_state *state)
{
   struct iris_context *ice = (void *) ctx;
   struct iris_screen *screen = (void *) ctx->screen;
   struct iris_uncompiled_shader *ish = iris_create_shader_state(ctx, state);

   // XXX: NOS?

   if (screen->precompile) {
      const struct gen_device_info *devinfo = &screen->devinfo;
      struct brw_gs_prog_key key = { KEY_INIT(devinfo->gen) };

      iris_compile_gs(ice, ish, &key);
   }

   return ish;
}

static void *
iris_create_fs_state(struct pipe_context *ctx,
                     const struct pipe_shader_state *state)
{
   struct iris_context *ice = (void *) ctx;
   struct iris_screen *screen = (void *) ctx->screen;
   struct iris_uncompiled_shader *ish = iris_create_shader_state(ctx, state);
   struct shader_info *info = &ish->nir->info;

   ish->nos |= (1ull << IRIS_NOS_FRAMEBUFFER) |
               (1ull << IRIS_NOS_DEPTH_STENCIL_ALPHA) |
               (1ull << IRIS_NOS_RASTERIZER) |
               (1ull << IRIS_NOS_BLEND);

   /* The program key needs the VUE map if there are > 16 inputs */
   if (util_bitcount64(ish->nir->info.inputs_read &
                       BRW_FS_VARYING_INPUT_MASK) > 16) {
      ish->nos |= (1ull << IRIS_NOS_LAST_VUE_MAP);
   }

   if (screen->precompile) {
      const uint64_t color_outputs = info->outputs_written &
         ~(BITFIELD64_BIT(FRAG_RESULT_DEPTH) |
           BITFIELD64_BIT(FRAG_RESULT_STENCIL) |
           BITFIELD64_BIT(FRAG_RESULT_SAMPLE_MASK));

      bool can_rearrange_varyings =
         util_bitcount64(info->inputs_read & BRW_FS_VARYING_INPUT_MASK) <= 16;

      const struct gen_device_info *devinfo = &screen->devinfo;
      struct brw_wm_prog_key key = {
         KEY_INIT(devinfo->gen),
         .nr_color_regions = util_bitcount(color_outputs),
         .coherent_fb_fetch = true,
         .input_slots_valid =
            can_rearrange_varyings ? 0 : info->inputs_read | VARYING_BIT_POS,
      };

      iris_compile_fs(ice, ish, &key, NULL);
   }

   return ish;
}

static void *
iris_create_compute_state(struct pipe_context *ctx,
                          const struct pipe_compute_state *state)
{
   assert(state->ir_type == PIPE_SHADER_IR_NIR);

   struct iris_context *ice = (void *) ctx;
   struct iris_screen *screen = (void *) ctx->screen;
   struct iris_uncompiled_shader *ish =
      iris_create_uncompiled_shader(ctx, (void *) state->prog, NULL);

   // XXX: disallow more than 64KB of shared variables

   if (screen->precompile) {
      const struct gen_device_info *devinfo = &screen->devinfo;
      struct brw_cs_prog_key key = { KEY_INIT(devinfo->gen) };

      iris_compile_cs(ice, ish, &key);
   }

   return ish;
}

/**
 * The pipe->delete_[stage]_state() driver hooks.
 *
 * Frees the iris_uncompiled_shader.
 */
static void
iris_delete_shader_state(struct pipe_context *ctx, void *state, gl_shader_stage stage)
{
   struct iris_uncompiled_shader *ish = state;
   struct iris_context *ice = (void *) ctx;

   if (ice->shaders.uncompiled[stage] == ish) {
      ice->shaders.uncompiled[stage] = NULL;
      ice->state.dirty |= IRIS_DIRTY_UNCOMPILED_VS << stage;
   }

   ralloc_free(ish->nir);
   free(ish);
}

static void
iris_delete_vs_state(struct pipe_context *ctx, void *state)
{
   iris_delete_shader_state(ctx, state, MESA_SHADER_VERTEX);
}

static void
iris_delete_tcs_state(struct pipe_context *ctx, void *state)
{
   iris_delete_shader_state(ctx, state, MESA_SHADER_TESS_CTRL);
}

static void
iris_delete_tes_state(struct pipe_context *ctx, void *state)
{
   iris_delete_shader_state(ctx, state, MESA_SHADER_TESS_EVAL);
}

static void
iris_delete_gs_state(struct pipe_context *ctx, void *state)
{
   iris_delete_shader_state(ctx, state, MESA_SHADER_GEOMETRY);
}

static void
iris_delete_fs_state(struct pipe_context *ctx, void *state)
{
   iris_delete_shader_state(ctx, state, MESA_SHADER_FRAGMENT);
}

static void
iris_delete_cs_state(struct pipe_context *ctx, void *state)
{
   iris_delete_shader_state(ctx, state, MESA_SHADER_COMPUTE);
}

/**
 * The pipe->bind_[stage]_state() driver hook.
 *
 * Binds an uncompiled shader as the current one for a particular stage.
 * Updates dirty tracking to account for the shader's NOS.
 */
static void
bind_state(struct iris_context *ice,
           struct iris_uncompiled_shader *ish,
           gl_shader_stage stage)
{
   uint64_t dirty_bit = IRIS_DIRTY_UNCOMPILED_VS << stage;
   const uint64_t nos = ish ? ish->nos : 0;

   const struct shader_info *old_info = iris_get_shader_info(ice, stage);
   const struct shader_info *new_info = ish ? &ish->nir->info : NULL;

   if ((old_info ? util_last_bit(old_info->textures_used) : 0) !=
       (new_info ? util_last_bit(new_info->textures_used) : 0)) {
      ice->state.dirty |= IRIS_DIRTY_SAMPLER_STATES_VS << stage;
   }

   ice->shaders.uncompiled[stage] = ish;
   ice->state.dirty |= dirty_bit;

   /* Record that CSOs need to mark IRIS_DIRTY_UNCOMPILED_XS when they change
    * (or that they no longer need to do so).
    */
   for (int i = 0; i < IRIS_NOS_COUNT; i++) {
      if (nos & (1 << i))
         ice->state.dirty_for_nos[i] |= dirty_bit;
      else
         ice->state.dirty_for_nos[i] &= ~dirty_bit;
   }
}

static void
iris_bind_vs_state(struct pipe_context *ctx, void *state)
{
   bind_state((void *) ctx, state, MESA_SHADER_VERTEX);
}

static void
iris_bind_tcs_state(struct pipe_context *ctx, void *state)
{
   bind_state((void *) ctx, state, MESA_SHADER_TESS_CTRL);
}

static void
iris_bind_tes_state(struct pipe_context *ctx, void *state)
{
   struct iris_context *ice = (struct iris_context *)ctx;

   /* Enabling/disabling optional stages requires a URB reconfiguration. */
   if (!!state != !!ice->shaders.uncompiled[MESA_SHADER_TESS_EVAL])
      ice->state.dirty |= IRIS_DIRTY_URB;

   bind_state((void *) ctx, state, MESA_SHADER_TESS_EVAL);
}

static void
iris_bind_gs_state(struct pipe_context *ctx, void *state)
{
   struct iris_context *ice = (struct iris_context *)ctx;

   /* Enabling/disabling optional stages requires a URB reconfiguration. */
   if (!!state != !!ice->shaders.uncompiled[MESA_SHADER_GEOMETRY])
      ice->state.dirty |= IRIS_DIRTY_URB;

   bind_state((void *) ctx, state, MESA_SHADER_GEOMETRY);
}

static void
iris_bind_fs_state(struct pipe_context *ctx, void *state)
{
   struct iris_context *ice = (struct iris_context *) ctx;
   struct iris_uncompiled_shader *old_ish =
      ice->shaders.uncompiled[MESA_SHADER_FRAGMENT];
   struct iris_uncompiled_shader *new_ish = state;

   const unsigned color_bits =
      BITFIELD64_BIT(FRAG_RESULT_COLOR) |
      BITFIELD64_RANGE(FRAG_RESULT_DATA0, BRW_MAX_DRAW_BUFFERS);

   /* Fragment shader outputs influence HasWriteableRT */
   if (!old_ish || !new_ish ||
       (old_ish->nir->info.outputs_written & color_bits) !=
       (new_ish->nir->info.outputs_written & color_bits))
      ice->state.dirty |= IRIS_DIRTY_PS_BLEND;

   bind_state((void *) ctx, state, MESA_SHADER_FRAGMENT);
}

static void
iris_bind_cs_state(struct pipe_context *ctx, void *state)
{
   bind_state((void *) ctx, state, MESA_SHADER_COMPUTE);
}

void
iris_init_program_functions(struct pipe_context *ctx)
{
   ctx->create_vs_state  = iris_create_vs_state;
   ctx->create_tcs_state = iris_create_tcs_state;
   ctx->create_tes_state = iris_create_tes_state;
   ctx->create_gs_state  = iris_create_gs_state;
   ctx->create_fs_state  = iris_create_fs_state;
   ctx->create_compute_state = iris_create_compute_state;

   ctx->delete_vs_state  = iris_delete_vs_state;
   ctx->delete_tcs_state = iris_delete_tcs_state;
   ctx->delete_tes_state = iris_delete_tes_state;
   ctx->delete_gs_state  = iris_delete_gs_state;
   ctx->delete_fs_state  = iris_delete_fs_state;
   ctx->delete_compute_state = iris_delete_cs_state;

   ctx->bind_vs_state  = iris_bind_vs_state;
   ctx->bind_tcs_state = iris_bind_tcs_state;
   ctx->bind_tes_state = iris_bind_tes_state;
   ctx->bind_gs_state  = iris_bind_gs_state;
   ctx->bind_fs_state  = iris_bind_fs_state;
   ctx->bind_compute_state = iris_bind_cs_state;
}
