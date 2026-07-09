/*
 * Copyright 2012 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "blorp_priv.h"
#include "blorp_nir_builder.h"
#include "brw/brw_compiler.h"
#include "brw/brw_nir.h"
#include "dev/intel_debug.h"

static debug_archiver *
blorp_debug_archiver_open(void *mem_ctx,
                         const struct nir_shader *nir,
                         const void *key,
                         unsigned key_size)
{
   if (!INTEL_DEBUG(DEBUG_MDA) || !INTEL_DEBUG(DEBUG_BLORP))
      return NULL;

   uint8_t blake3[BLAKE3_KEY_LEN];
   _mesa_blake3_compute(key, key_size, blake3);
   char name[BLAKE3_HEX_LEN + 6] = {};
   _mesa_blake3_format(name, blake3);
   memcpy(&name[BLAKE3_HEX_LEN - 1], ".blorp", 6);

   debug_archiver *archiver = debug_archiver_open(mem_ctx, name, "blorp");
   debug_archiver_set_prefix(
      archiver,
      _mesa_shader_stage_to_abbrev(nir->info.stage));

   return archiver;
}

static const nir_shader_compiler_options *
blorp_nir_options_brw(struct blorp_context *blorp,
                      mesa_shader_stage stage)
{
   const struct brw_compiler *compiler = blorp->compiler->brw;
   return &compiler->nir_options[stage];
}

static struct blorp_program
blorp_compile_fs_brw(struct blorp_context *blorp, void *mem_ctx,
                     struct nir_shader *nir,
                     bool multisample_fbo,
                     bool is_fast_clear,
                     bool use_repclear,
                     const void *key, uint32_t key_size)
{
   const struct brw_compiler *compiler = blorp->compiler->brw;

   struct brw_fs_prog_data *fs_prog_data = rzalloc(mem_ctx, struct brw_fs_prog_data);

   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   struct brw_nir_compiler_opts opts = {
      .softfp64 = ((nir->info.bit_sizes_float & 64) &&
                   !compiler->devinfo->has_64bit_float) ?
                  blorp->get_fp64_nir(blorp) : NULL,
   };
   brw_preprocess_nir(compiler, nir, &opts);
   nir_remove_dead_variables(nir, nir_var_shader_in, NULL);
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
   if (is_fast_clear || use_repclear) {
      nir->info.api_subgroup_size = 16;
      nir->info.max_subgroup_size = 16;
      nir->info.min_subgroup_size = 16;
   }

   struct brw_fs_prog_key wm_key;
   memset(&wm_key, 0, sizeof(wm_key));
   wm_key.multisample_fbo = multisample_fbo ? INTEL_ALWAYS : INTEL_NEVER;
   wm_key.nr_color_regions = 1;

   debug_archiver *archiver =
      blorp_debug_archiver_open(mem_ctx, nir, key, key_size);

   struct brw_compile_fs_params params = {
      .base = {
         .mem_ctx = mem_ctx,
         .nir = nir,
         .key = &wm_key.base,
         .prog_data = (struct brw_stage_prog_data *)fs_prog_data,
         .log_data = blorp->driver_ctx,
         .debug_flag = DEBUG_BLORP,
         .archiver = archiver,
      },

      .use_rep_send = use_repclear,
      .max_polygons = 1,
   };

   const unsigned *kernel = brw_compile(compiler, &params.base);

   debug_archiver_close(archiver);

   return (struct blorp_program){
      .kernel         = kernel,
      .kernel_size    = fs_prog_data->base.program_size,
      .prog_data      = fs_prog_data,
      .prog_data_size = sizeof(*fs_prog_data),
   };
}

static struct blorp_program
blorp_compile_vs_brw(struct blorp_context *blorp, void *mem_ctx,
                     struct nir_shader *nir,
                     const void *key, uint32_t key_size)
{
   const struct brw_compiler *compiler = blorp->compiler->brw;

   struct brw_nir_compiler_opts opts = {};
   brw_preprocess_nir(compiler, nir, &opts);
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   struct brw_vs_prog_data *vs_prog_data = rzalloc(mem_ctx, struct brw_vs_prog_data);
   vs_prog_data->inputs_read = nir->info.inputs_read;

   brw_compute_vue_map(compiler->devinfo,
                       &vs_prog_data->base.vue_map,
                       nir->info.outputs_written,
                       nir->info.separate_shader,
                       1);

   struct brw_vs_prog_key vs_key = { 0, };

   debug_archiver *archiver =
      blorp_debug_archiver_open(mem_ctx, nir, key, key_size);

   struct brw_compile_vs_params params = {
      .base = {
         .mem_ctx = mem_ctx,
         .nir = nir,
         .key = &vs_key.base,
         .prog_data = (struct brw_stage_prog_data *)vs_prog_data,
         .log_data = blorp->driver_ctx,
         .debug_flag = DEBUG_BLORP,
         .archiver = archiver,
      },
   };

   const unsigned *kernel = brw_compile(compiler, &params.base);

   debug_archiver_close(archiver);

   return (struct blorp_program) {
      .kernel         = kernel,
      .kernel_size    = vs_prog_data->base.base.program_size,
      .prog_data      = vs_prog_data,
      .prog_data_size = sizeof(*vs_prog_data),
   };
}

static bool
lower_base_workgroup_id(nir_builder *b, nir_intrinsic_instr *intrin,
                        UNUSED void *data)
{
   if (intrin->intrinsic != nir_intrinsic_load_base_workgroup_id)
      return false;

   b->cursor = nir_instr_remove(&intrin->instr);
   nir_def_rewrite_uses(&intrin->def, nir_imm_zero(b, 3, 32));
   return true;
}

static bool
lower_load_uniform(nir_builder *b, nir_intrinsic_instr *intrin, void *data)
{
   if (intrin->intrinsic != nir_intrinsic_load_uniform)
      return false;

   const struct intel_device_info *devinfo = data;

   b->cursor = nir_instr_remove(&intrin->instr);
   nir_def *value;
   if (b->shader->info.stage == MESA_SHADER_COMPUTE &&
       devinfo->verx10 >= 125) {
      value = nir_load_shader_indirect_data_intel(
         b,
         intrin->def.num_components,
         intrin->def.bit_size,
         nir_iadd(b, nir_load_indirect_address_intel(b), intrin->src[0].ssa),
         .base = nir_intrinsic_base(intrin),
         .range = nir_intrinsic_range(intrin));
   } else {
      value = nir_load_push_data_intel(b,
                                       intrin->def.num_components,
                                       intrin->def.bit_size,
                                       intrin->src[0].ssa,
                                       .base = nir_intrinsic_base(intrin),
                                       .range = nir_intrinsic_range(intrin));
   }
   nir_def_rewrite_uses(&intrin->def, value);
   return true;
}

static struct blorp_program
blorp_compile_cs_brw(struct blorp_context *blorp, void *mem_ctx,
                     struct nir_shader *nir,
                     const void *key, uint32_t key_size)
{
   const struct brw_compiler *compiler = blorp->compiler->brw;

   struct brw_nir_compiler_opts opts = {};
   brw_preprocess_nir(compiler, nir, &opts);
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   NIR_PASS(_, nir, nir_lower_io, nir_var_uniform, type_size_scalar_bytes,
              (nir_lower_io_options)0);

   NIR_PASS(_, nir, nir_shader_intrinsics_pass, lower_load_uniform,
               nir_metadata_control_flow, (void *) compiler->devinfo);

   STATIC_ASSERT(offsetof(struct blorp_wm_inputs, subgroup_id) + 4 ==
                 sizeof(struct blorp_wm_inputs));

   struct brw_cs_prog_data *cs_prog_data = rzalloc(mem_ctx, struct brw_cs_prog_data);
   cs_prog_data->base.push_sizes[0] = sizeof(struct blorp_wm_inputs);

   brw_cs_fill_push_const_info(compiler->devinfo, cs_prog_data,
                               offsetof(struct blorp_wm_inputs, subgroup_id) / 4);
   cs_prog_data->base.push_sizes[0] = align(cs_prog_data->base.push_sizes[0], REG_SIZE);

   NIR_PASS(_, nir, brw_nir_lower_cs_intrinsics, compiler->devinfo,
              cs_prog_data);
   NIR_PASS(_, nir, brw_nir_lower_cs_subgroup_id, compiler->devinfo,
               offsetof(struct blorp_wm_inputs, subgroup_id));
   NIR_PASS(_, nir, nir_shader_intrinsics_pass, lower_base_workgroup_id,
               nir_metadata_control_flow, NULL);

   struct brw_cs_prog_key cs_key;
   memset(&cs_key, 0, sizeof(cs_key));

   debug_archiver *archiver =
      blorp_debug_archiver_open(mem_ctx, nir, key, key_size);

   struct brw_compile_cs_params params = {
      .base = {
         .mem_ctx = mem_ctx,
         .nir = nir,
         .key = &cs_key.base,
         .prog_data = (struct brw_stage_prog_data *)cs_prog_data,
         .log_data = blorp->driver_ctx,
         .debug_flag = DEBUG_BLORP,
         .archiver = archiver,
      },
   };

   const unsigned *kernel = brw_compile(compiler, &params.base);

   debug_archiver_close(archiver);

   return (struct blorp_program) {
      .kernel         = kernel,
      .kernel_size    = cs_prog_data->base.program_size,
      .prog_data      = cs_prog_data,
      .prog_data_size = sizeof(*cs_prog_data),
   };
}

#pragma pack(push, 1)
struct layer_offset_vs_key {
   struct blorp_base_key base;
   unsigned num_inputs;
};
#pragma pack(pop)

/* In the case of doing attachment clears, we are using a surface state that
 * is handed to us so we can't set (and don't even know) the base array layer.
 * In order to do a layered clear in this scenario, we need some way of adding
 * the base array layer to the instance id.  Unfortunately, our hardware has
 * no real concept of "base instance", so we have to do it manually in a
 * vertex shader.
 */
static bool
blorp_params_get_layer_offset_vs_brw(struct blorp_batch *batch,
                                     struct blorp_params *params)
{
   struct blorp_context *blorp = batch->blorp;
   struct layer_offset_vs_key blorp_key;
   BLORP_KEY_INIT(blorp_key, BLORP_SHADER_TYPE_LAYER_OFFSET_VS,
                  BLORP_SHADER_PIPELINE_RENDER);

   struct brw_fs_prog_data *fs_prog_data = params->fs_prog_data;
   if (fs_prog_data)
      blorp_key.num_inputs = fs_prog_data->num_varying_inputs;

   if (blorp->lookup_shader(batch, &blorp_key, sizeof(blorp_key),
                            &params->vs_prog_kernel, &params->vs_prog_data))
      return true;

   void *mem_ctx = ralloc_context(NULL);

   nir_builder b;
   blorp_nir_init_shader(&b, blorp, mem_ctx, MESA_SHADER_VERTEX,
                         blorp_shader_type_to_name(blorp_key.base.shader_type));

   const struct glsl_type *uvec4_type = glsl_vector_type(GLSL_TYPE_UINT, 4);

   /* First we deal with the header which has instance and base instance */
   nir_variable *a_header = nir_variable_create(b.shader, nir_var_shader_in,
                                                uvec4_type, "header");
   a_header->data.location = VERT_ATTRIB_GENERIC0;

   nir_variable *v_layer = nir_variable_create(b.shader, nir_var_shader_out,
                                               glsl_int_type(), "layer_id");
   v_layer->data.location = VARYING_SLOT_LAYER;

   /* Compute the layer id */
   nir_def *header = nir_load_var(&b, a_header);
   nir_def *base_layer = nir_channel(&b, header, 0);
   nir_def *instance = nir_channel(&b, header, 1);
   nir_store_var(&b, v_layer, nir_iadd(&b, instance, base_layer), 0x1);

   /* Then we copy the vertex from the next slot to VARYING_SLOT_POS */
   nir_variable *a_vertex = nir_variable_create(b.shader, nir_var_shader_in,
                                                glsl_vec4_type(), "a_vertex");
   a_vertex->data.location = VERT_ATTRIB_GENERIC1;

   nir_variable *v_pos = nir_variable_create(b.shader, nir_var_shader_out,
                                             glsl_vec4_type(), "v_pos");
   v_pos->data.location = VARYING_SLOT_POS;

   nir_copy_var(&b, v_pos, a_vertex);

   /* Then we copy everything else */
   for (unsigned i = 0; i < blorp_key.num_inputs; i++) {
      nir_variable *a_in = nir_variable_create(b.shader, nir_var_shader_in,
                                               uvec4_type, "input");
      a_in->data.location = VERT_ATTRIB_GENERIC2 + i;

      nir_variable *v_out = nir_variable_create(b.shader, nir_var_shader_out,
                                                uvec4_type, "output");
      v_out->data.location = VARYING_SLOT_VAR0 + i;

      nir_copy_var(&b, v_out, a_in);
   }

   const struct blorp_program p =
      blorp_compile_vs(blorp, mem_ctx, b.shader, &blorp_key, sizeof(blorp_key));

   bool result =
      blorp->upload_shader(batch, MESA_SHADER_VERTEX,
                           &blorp_key, sizeof(blorp_key),
                           p.kernel, p.kernel_size,
                           p.prog_data, p.prog_data_size,
                           &params->vs_prog_kernel, &params->vs_prog_data);

   ralloc_free(mem_ctx);
   return result;
}

void
blorp_init_brw(struct blorp_context *blorp, void *driver_ctx,
               struct isl_device *isl_dev, const struct brw_compiler *brw,
               const struct blorp_config *config)
{
   blorp_init(blorp, driver_ctx, isl_dev, config);
   assert(brw);

   blorp->compiler->brw = brw;
   blorp->compiler->nir_options = blorp_nir_options_brw;
   blorp->compiler->compile_fs = blorp_compile_fs_brw;
   blorp->compiler->compile_vs = blorp_compile_vs_brw;
   blorp->compiler->compile_cs = blorp_compile_cs_brw;
   blorp->compiler->params_get_layer_offset_vs =
      blorp_params_get_layer_offset_vs_brw;
}
