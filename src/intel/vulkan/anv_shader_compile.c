/* Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "anv_private.h"
#include "anv_nir.h"
#include "anv_shader.h"

#include "vk_nir_convert_ycbcr.h"
#include "vk_pipeline.h"

#include "common/intel_compute_slm.h"
#include "common/intel_l3_config.h"

#include "compiler/brw/brw_nir.h"
#include "compiler/brw/brw_nir_rt.h"
#include "compiler/intel_nir.h"

#include "git_sha1.h"

typedef void (*game_wa_callback)(nir_shader *nir);

/* Structure to hold a game-specific workaround entry */
struct game_wa_entry {
   game_wa_callback cb;
   uint32_t shader_blake3s[16][BLAKE3_OUT_LEN32];
};

/* Workaround for a shader in Horizon Forbidden West that causes
 * visual corruption.  The shader writes the result of fsqrt to
 * storage images with a 16-bit image format and misrendering
 * occurs when those values are denormal for an unknown reason.
 *
 * This clamps the image writes to the smallest fp16 normalized
 * value.  (Pattern matching against fsqrt is easy to do in a one
 * line algebraic pass, while matching image stores is harder.)
 *
 * See https://gitlab.freedesktop.org/mesa/mesa/-/issues/12555
 */
static void
wa_forbidden_west(nir_shader *nir)
{
   NIR_PASS(_, nir, brw_nir_apply_sqrt_workarounds);
}

/* Try to detect shaders relying on 32-wide subgroups. Usually they have a
 * pattern like this:
 *
 * div 32    %1096 = @load_subgroup_invocation
 * div 32    %1245 = iand %1096, %1228 (0x1f)
 * div 32    %1246 = ixor %1245, %13 (0x1)
 * div 32    %1247 = @shuffle (%1244, %1246)
 */
static bool is_alu1_iand_0x1f(nir_alu_instr *alu)
{
   if (!alu || alu->op != nir_op_iand)
      return false;

   for (uint32_t i = 0; i < 2; i++) {
       if (nir_src_is_const(alu->src[i].src) &&
           nir_alu_src_as_uint(alu->src[i]) == 0x1f)
          return true;
   }

   return false;
}

static bool
detect_simd32_shuffle(nir_builder *b,
                      nir_intrinsic_instr *intrin,
                      void *data)
{
   if (intrin->intrinsic != nir_intrinsic_shuffle)
      return false;

   nir_alu_instr *alu1 = nir_src_as_alu(intrin->src[1]);
   if (alu1 == NULL)
      return false;

   if (is_alu1_iand_0x1f(alu1))
      return true;

   for (uint32_t i = 0; i < nir_op_infos[alu1->op].num_inputs; i++) {
      if (is_alu1_iand_0x1f(nir_src_as_alu(alu1->src[i].src)))
         return true;
   }

   return false;
}

/* List of game-specific workarounds identified by BLAKE3 hash of the shader.
 * Add new workarounds here as needed.
 */
static const struct game_wa_entry game_was[] = {
   {
      .cb = wa_forbidden_west,
      .shader_blake3s = {
         {0x51683151, 0xe044f0ce, 0xc210a762, 0xb12b2da4, 0x4e69ddc0, 0x237b1cc1, 0xc84bcf09, 0x31cfe883},
      },
   },
};

/* Apply game-specific workarounds based on the shader's BLAKE3 hash */
static void
anv_nir_apply_shader_workarounds(nir_shader *nir)
{
   for (unsigned i = 0; i < ARRAY_SIZE(game_was); i++) {
      const struct game_wa_entry *wa = &game_was[i];
      for (unsigned j = 0; j < ARRAY_SIZE(wa->shader_blake3s); j++) {
         if (_mesa_printed_blake3_equal(nir->info.source_blake3, wa->shader_blake3s[j])) {
            wa->cb(nir);
            return;
         }
      }
   }
}

static enum brw_robustness_flags
anv_get_robust_flags(const struct vk_pipeline_robustness_state *rstate)
{
   return
      ((rstate->storage_buffers !=
        VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT) ?
       BRW_ROBUSTNESS_SSBO : 0) |
      ((rstate->uniform_buffers !=
        VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT) ?
       BRW_ROBUSTNESS_UBO : 0);
}

static enum anv_descriptor_set_layout_type
set_layouts_get_layout_type(struct anv_descriptor_set_layout * const *set_layouts,
                            uint32_t set_layout_count)
{
   for (uint32_t s = 0; s < set_layout_count; s++) {
      if (set_layouts[s]) {
         return set_layouts[s]->type;
      }
   }

   return ANV_PIPELINE_DESCRIPTOR_SET_LAYOUT_TYPE_UNKNOWN;
}

void
anv_shader_init_uuid(struct anv_physical_device *device)
{
   /* We should include any parameter here that will change the compiler's
    * output. Mostly it's workarounds, but there is also settings for using
    * indirect descriptors (a different binding model).
    *
    * The fp64 workaround is skipped because although it changes the
    * compiler's output, not having that workaroung enabled with an app
    * expecting fp64 support will just crash in the backend.
    */
   struct mesa_sha1 ctx;
   _mesa_sha1_init(&ctx);

   const bool indirect_descriptors = device->indirect_descriptors;
   _mesa_sha1_update(&ctx, &indirect_descriptors, sizeof(indirect_descriptors));

   const int spilling_rate = device->compiler->spilling_rate;
   _mesa_sha1_update(&ctx, &spilling_rate, sizeof(spilling_rate));

   const uint8_t afs = device->instance->assume_full_subgroups;
   _mesa_sha1_update(&ctx, &afs, sizeof(afs));

   const bool afswb = device->instance->assume_full_subgroups_with_barrier;
   _mesa_sha1_update(&ctx, &afswb, sizeof(afswb));

   const bool afs_shm = device->instance->assume_full_subgroups_with_shared_memory;
   _mesa_sha1_update(&ctx, &afs_shm, sizeof(afs_shm));

   const bool erwf = device->instance->emulate_read_without_format;
   _mesa_sha1_update(&ctx, &erwf, sizeof(erwf));

   const bool lttd = device->instance->lower_terminate_to_discard;
   _mesa_sha1_update(&ctx, &lttd, sizeof(lttd));

   const bool large_wg_wa =
      device->instance->large_workgroup_non_coherent_image_workaround;
   _mesa_sha1_update(&ctx, &large_wg_wa, sizeof(large_wg_wa));

   uint8_t sha1[20];
   _mesa_sha1_final(&ctx, sha1);
   memcpy(device->shader_binary_uuid, sha1, sizeof(device->shader_binary_uuid));
}

static const struct nir_shader_compiler_options *
anv_shader_get_nir_options(struct vk_physical_device *device,
                           mesa_shader_stage stage,
                           const struct vk_pipeline_robustness_state *rs)

{
   struct anv_physical_device *pdevice =
      container_of(device, struct anv_physical_device, vk);
   const struct brw_compiler *compiler = pdevice->compiler;

   return &compiler->nir_options[stage];
}

static struct spirv_to_nir_options
anv_shader_get_spirv_options(struct vk_physical_device *device,
                             mesa_shader_stage stage,
                             const struct vk_pipeline_robustness_state *rs)
{
   struct anv_physical_device *pdevice =
      container_of(device, struct anv_physical_device, vk);
   enum brw_robustness_flags robust_flags = anv_get_robust_flags(rs);

   return (struct spirv_to_nir_options) {
      .ubo_addr_format = anv_nir_ubo_addr_format(pdevice, robust_flags),
      .ssbo_addr_format = anv_nir_ssbo_addr_format(pdevice, robust_flags),
      .phys_ssbo_addr_format = nir_address_format_64bit_global,
      .push_const_addr_format = nir_address_format_logical,

      .printf = INTEL_DEBUG(DEBUG_SHADER_PRINT),

      /* TODO: Consider changing this to an address format that has the NULL
       * pointer equals to 0.  That might be a better format to play nice
       * with certain code / code generators.
       */
      .shared_addr_format = nir_address_format_32bit_offset,

      .min_ubo_alignment = ANV_UBO_ALIGNMENT,
      .min_ssbo_alignment = ANV_SSBO_ALIGNMENT,

      .workarounds = {
         .lower_terminate_to_discard = pdevice->instance->lower_terminate_to_discard,
      },
   };
}

static void
anv_shader_preprocess_nir(struct vk_physical_device *device,
                          nir_shader *nir,
                          const struct vk_pipeline_robustness_state *rs)
{
   struct anv_physical_device *pdevice =
      container_of(device, struct anv_physical_device, vk);
   const struct brw_compiler *compiler = pdevice->compiler;

   NIR_PASS(_, nir, nir_lower_io_vars_to_temporaries,
            nir_shader_get_entrypoint(nir), nir_var_shader_out);

   const struct nir_lower_sysvals_to_varyings_options sysvals_to_varyings = {
      .point_coord = true,
   };
   NIR_PASS(_, nir, nir_lower_sysvals_to_varyings, &sysvals_to_varyings);

   NIR_PASS(_, nir, nir_update_image_intrinsic_from_var);

   const nir_opt_access_options opt_access_options = {
      .is_vulkan = true,
   };
   NIR_PASS(_, nir, nir_opt_access, &opt_access_options);

   struct brw_nir_compiler_opts opts = {
      .robust_image_access = rs->images == VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_ROBUST_IMAGE_ACCESS ||
                             rs->images == VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_ROBUST_IMAGE_ACCESS_2_EXT,
   };
   brw_preprocess_nir(compiler, nir, &opts);

   NIR_PASS(_, nir, nir_opt_barrier_modes);
   NIR_PASS(_, nir, nir_opt_acquire_release_barriers, SCOPE_QUEUE_FAMILY);

   if (INTEL_DEBUG(DEBUG_SHADER_PRINT)) {
      const nir_lower_printf_options printf_opts = {
         .ptr_bit_size = 64,
         .hash_format_strings = true,
      };
      NIR_PASS(_, nir, nir_lower_printf, &printf_opts);
   }

   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
}

static void
populate_base_prog_key(struct brw_base_prog_key *key,
                       const struct vk_physical_device *device,
                       const struct vk_pipeline_robustness_state *rs)
{
   const struct anv_physical_device *pdevice =
      container_of(device, const struct anv_physical_device, vk);

   /* We can avoid including this for hashing because the runtime already
    * hashes that information. We just put it here for at compile time.
    */
   if (rs != NULL)
      key->robust_flags = anv_get_robust_flags(rs);
   key->limit_trig_input_range = pdevice->instance->limit_trig_input_range;
}

static void
populate_base_gfx_prog_key(struct brw_base_prog_key *key,
                           const struct vk_physical_device *device,
                           const struct vk_pipeline_robustness_state *rs,
                           const struct vk_graphics_pipeline_state *gfx_state,
                           VkShaderStageFlags link_stages)
{
   const struct anv_physical_device *pdevice =
      container_of(device, const struct anv_physical_device, vk);

   populate_base_prog_key(key, device, rs);

   key->view_mask = (gfx_state && gfx_state->rp) ? gfx_state->rp->view_mask : 0;

   key->vue_layout =
      (util_bitcount(link_stages) > 1 && (link_stages & VK_SHADER_STAGE_FRAGMENT_BIT)) ?
      INTEL_VUE_LAYOUT_FIXED :
      pdevice->info.verx10 >= 125 ? INTEL_VUE_LAYOUT_SEPARATE_MESH :
      INTEL_VUE_LAYOUT_SEPARATE;
}

static void
populate_vs_prog_key(struct brw_vs_prog_key *key,
                     const struct vk_physical_device *device,
                     const struct vk_pipeline_robustness_state *rs,
                     const struct vk_graphics_pipeline_state *state,
                     VkShaderStageFlags link_stages)
{
   const struct anv_physical_device *pdevice =
      container_of(device, const struct anv_physical_device, vk);

   populate_base_gfx_prog_key(&key->base, device, rs, state, link_stages);

   key->vf_component_packing = pdevice->instance->vf_component_packing;
}

static void
populate_tcs_prog_key(struct brw_tcs_prog_key *key,
                      const struct vk_physical_device *device,
                      const struct vk_pipeline_robustness_state *rs,
                      const struct vk_graphics_pipeline_state *state,
                      VkShaderStageFlags link_stages)
{
   populate_base_gfx_prog_key(&key->base, device, rs, state, link_stages);

   if (state && state->ts &&
       !BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_TS_PATCH_CONTROL_POINTS))
      key->input_vertices = state->ts->patch_control_points;

   key->separate_tess_vue_layout =
      !(link_stages & VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);
}

static void
populate_tes_prog_key(struct brw_tes_prog_key *key,
                      const struct vk_physical_device *device,
                      const struct vk_pipeline_robustness_state *rs,
                      const struct vk_graphics_pipeline_state *state,
                      VkShaderStageFlags link_stages)
{
   populate_base_gfx_prog_key(&key->base, device, rs, state, link_stages);

   key->separate_tess_vue_layout =
      !(link_stages & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
}

static void
populate_gs_prog_key(struct brw_gs_prog_key *key,
                     const struct vk_physical_device *device,
                     const struct vk_pipeline_robustness_state *rs,
                     const struct vk_graphics_pipeline_state *state,
                     VkShaderStageFlags link_stages)
{
   populate_base_gfx_prog_key(&key->base, device, rs, state, link_stages);
}

static void
populate_task_prog_key(struct brw_task_prog_key *key,
                       const struct vk_physical_device *device,
                       const struct vk_pipeline_robustness_state *rs,
                       const struct vk_graphics_pipeline_state *state,
                       VkShaderStageFlags link_stages)
{
   populate_base_gfx_prog_key(&key->base, device, rs, state, link_stages);

   key->base.uses_inline_push_addr = true;
}

static void
populate_mesh_prog_key(struct brw_mesh_prog_key *key,
                       const struct vk_physical_device *device,
                       const struct vk_pipeline_robustness_state *rs,
                       const struct vk_graphics_pipeline_state *state,
                       VkShaderStageFlags link_stages)
{
   populate_base_gfx_prog_key(&key->base, device, rs, state, link_stages);

   key->base.uses_inline_push_addr = true;
}

static bool
pipeline_has_coarse_pixel(const struct vk_graphics_pipeline_state *state)
{
   if (state == NULL)
      return true;

   /* The Vulkan 1.2.199 spec says:
    *
    *    "If any of the following conditions are met, Cxy' must be set to
    *    {1,1}:
    *
    *     * If Sample Shading is enabled.
    *     * [...]"
    *
    * And "sample shading" is defined as follows:
    *
    *    "Sample shading is enabled for a graphics pipeline:
    *
    *     * If the interface of the fragment shader entry point of the
    *       graphics pipeline includes an input variable decorated with
    *       SampleId or SamplePosition. In this case minSampleShadingFactor
    *       takes the value 1.0.
    *
    *     * Else if the sampleShadingEnable member of the
    *       VkPipelineMultisampleStateCreateInfo structure specified when
    *       creating the graphics pipeline is set to VK_TRUE. In this case
    *       minSampleShadingFactor takes the value of
    *       VkPipelineMultisampleStateCreateInfo::minSampleShading.
    *
    *    Otherwise, sample shading is considered disabled."
    *
    * The first bullet above is handled by the back-end compiler because those
    * inputs both force per-sample dispatch.  The second bullet is handled
    * here.  Note that this sample shading being enabled has nothing to do
    * with minSampleShading.
    */
   if (state->ms != NULL && state->ms->sample_shading_enable)
      return false;

   /* Not dynamic & pipeline has a 1x1 fragment shading rate with no
    * possibility for element of the pipeline to change the value or fragment
    * shading rate not specified at all.
    */
   if (!BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_FSR) &&
       (state->fsr == NULL ||
        (state->fsr->fragment_size.width <= 1 &&
         state->fsr->fragment_size.height <= 1 &&
         state->fsr->combiner_ops[0] == VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR &&
         state->fsr->combiner_ops[1] == VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR)))
      return false;

   return true;
}

static uint32_t
rp_color_mask(const struct vk_graphics_pipeline_state *state)
{
   if (state == NULL || state->rp == NULL ||
       !vk_render_pass_state_has_attachment_info(state->rp))
      return ((1u << MAX_RTS) - 1);

   assert(state->rp->color_attachment_count <= MAX_RTS);

   uint32_t color_mask = 0;
   for (uint32_t i = 0; i < state->rp->color_attachment_count; i++) {
      if (state->rp->color_attachment_formats[i] != VK_FORMAT_UNDEFINED) {
         if (state->cal) {
            if (state->cal->color_map[i] != MESA_VK_ATTACHMENT_UNUSED)
               color_mask |= BITFIELD_BIT(state->cal->color_map[i]);
         } else {
            color_mask |= BITFIELD_BIT(i);
         }
      }
   }

   return color_mask;
}

static void
populate_wm_prog_key(struct brw_wm_prog_key *key,
                     const struct vk_physical_device *device,
                     const struct vk_pipeline_robustness_state *rs,
                     const struct vk_graphics_pipeline_state *state,
                     VkShaderStageFlags link_stages)
{
   const struct anv_physical_device *pdevice =
      container_of(device, const struct anv_physical_device, vk);

   populate_base_gfx_prog_key(&key->base, device, rs, state, link_stages);

   /* Consider all inputs as valid until look at the NIR variables. */
   key->color_outputs_valid = rp_color_mask(state);
   key->nr_color_regions = util_last_bit(key->color_outputs_valid);

   /* To reduce possible shader recompilations we would need to know if
    * there is a SampleMask output variable to compute if we should emit
    * code to workaround the issue that hardware disables alpha to coverage
    * when there is SampleMask output.
    *
    * If the pipeline we compile the fragment shader in includes the output
    * interface, then we can be sure whether alpha_coverage is enabled or not.
    * If we don't have that output interface, then we have to compile the
    * shader with some conditionals.
    */
   if (state != NULL && state->ms != NULL) {
      /* VUID-VkGraphicsPipelineCreateInfo-rasterizerDiscardEnable-00751:
       *
       *   "If the pipeline is being created with fragment shader state,
       *    pMultisampleState must be a valid pointer to a valid
       *    VkPipelineMultisampleStateCreateInfo structure"
       *
       * It's also required for the fragment output interface.
       */
      key->multisample_fbo =
         BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_MS_RASTERIZATION_SAMPLES) ?
         INTEL_SOMETIMES :
         state->ms->rasterization_samples > 1 ? INTEL_ALWAYS : INTEL_NEVER;
      key->persample_interp =
         BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_MS_RASTERIZATION_SAMPLES) ?
         INTEL_SOMETIMES :
         (state->ms->sample_shading_enable &&
          (state->ms->min_sample_shading * state->ms->rasterization_samples) > 1) ?
         INTEL_ALWAYS : INTEL_NEVER;
      key->alpha_to_coverage =
         BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_MS_ALPHA_TO_COVERAGE_ENABLE) ?
         INTEL_SOMETIMES :
         (state->ms->alpha_to_coverage_enable ? INTEL_ALWAYS : INTEL_NEVER);

      /* TODO: We should make this dynamic */
      if (pdevice->instance->sample_mask_out_opengl_behaviour)
         key->ignore_sample_mask_out = !key->multisample_fbo;
   } else {
      /* Consider all inputs as valid until we look at the NIR variables. */
      key->color_outputs_valid = BITFIELD_MASK(MAX_RTS);
      key->nr_color_regions = MAX_RTS;

      key->alpha_to_coverage = INTEL_SOMETIMES;
      key->multisample_fbo = INTEL_SOMETIMES;
      key->persample_interp = INTEL_SOMETIMES;
   }

   if (pdevice->info.verx10 >= 200) {
      if (state != NULL && state->rs != NULL) {
         key->provoking_vertex_last =
            BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_RS_PROVOKING_VERTEX) ?
            INTEL_SOMETIMES :
            state->rs->provoking_vertex == VK_PROVOKING_VERTEX_MODE_LAST_VERTEX_EXT ?
            INTEL_ALWAYS : INTEL_NEVER;
      } else {
         key->provoking_vertex_last = INTEL_SOMETIMES;
      }
   } else {
      /* Pre-Xe2 we don't care about this at all, make sure it's always set to
       * NEVER to avoid it influencing the push constant.
       */
      key->provoking_vertex_last = INTEL_NEVER;
   }

   key->mesh_input =
      (link_stages & VK_SHADER_STAGE_VERTEX_BIT) ? INTEL_NEVER :
      (link_stages & VK_SHADER_STAGE_MESH_BIT_EXT) ? INTEL_ALWAYS :
      pdevice->info.verx10 >= 125 ? INTEL_SOMETIMES : INTEL_NEVER;

   if (state && state->ms) {
      key->min_sample_shading = state->ms->min_sample_shading;
      key->api_sample_shading = state->ms->sample_shading_enable;
   }

   key->coarse_pixel = pipeline_has_coarse_pixel(state);

   key->null_push_constant_tbimr_workaround =
      pdevice->info.needs_null_push_constant_tbimr_workaround;
}

static void
populate_cs_prog_key(struct brw_cs_prog_key *key,
                     const struct vk_physical_device *device,
                     const struct vk_pipeline_robustness_state *rs,
                     bool lower_unaligned_dispatch)
{
   const struct anv_physical_device *pdevice =
      container_of(device, const struct anv_physical_device, vk);

   populate_base_prog_key(&key->base, device, rs);

   key->base.uses_inline_push_addr = pdevice->info.verx10 >= 125;
   key->lower_unaligned_dispatch = lower_unaligned_dispatch;
}

static void
populate_bs_prog_key(struct brw_bs_prog_key *key,
                     const struct vk_physical_device *device,
                     const struct vk_pipeline_robustness_state *rs,
                     VkPipelineCreateFlags2KHR flags)
{
   populate_base_prog_key(&key->base, device, rs);

   uint32_t ray_flags = 0;
   const bool rt_skip_triangles =
      flags & VK_PIPELINE_CREATE_2_RAY_TRACING_SKIP_TRIANGLES_BIT_KHR;
   const bool rt_skip_aabbs =
      flags & VK_PIPELINE_CREATE_2_RAY_TRACING_SKIP_AABBS_BIT_KHR;
   assert(!(rt_skip_triangles && rt_skip_aabbs));
   if (rt_skip_triangles)
      ray_flags |= BRW_RT_RAY_FLAG_SKIP_TRIANGLES;
   else if (rt_skip_aabbs)
      ray_flags |= BRW_RT_RAY_FLAG_SKIP_AABBS;

   key->pipeline_ray_flags = ray_flags;
}

static void
anv_shader_hash_state(struct vk_physical_device *device,
                      const struct vk_graphics_pipeline_state *state,
                      const struct vk_features *enabled_features,
                      VkShaderStageFlags stages,
                      blake3_hash blake3_out)
{
   struct mesa_blake3 blake3_ctx;
   _mesa_blake3_init(&blake3_ctx);

   anv_foreach_vk_stage(stage, stages) {
      union brw_any_prog_key key;
      memset(&key, 0, sizeof(key));

      switch (stage) {
      case VK_SHADER_STAGE_VERTEX_BIT:
         populate_vs_prog_key(&key.vs, device, NULL, state, stages);
         _mesa_blake3_update(&blake3_ctx, &key.vs, sizeof(key.vs));
         break;
      case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
         populate_tcs_prog_key(&key.tcs, device, NULL, state, stages);
         _mesa_blake3_update(&blake3_ctx, &key.tcs, sizeof(key.tcs));
         break;
      case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
         populate_tes_prog_key(&key.tes, device, NULL, state, stages);
         _mesa_blake3_update(&blake3_ctx, &key.tes, sizeof(key.tes));
         break;
      case VK_SHADER_STAGE_GEOMETRY_BIT:
         populate_gs_prog_key(&key.gs, device, NULL, state, stages);
         _mesa_blake3_update(&blake3_ctx, &key.gs, sizeof(key.gs));
         break;
      case VK_SHADER_STAGE_TASK_BIT_EXT:
         populate_task_prog_key(&key.task, device, NULL, state, stages);
         _mesa_blake3_update(&blake3_ctx, &key.task, sizeof(key.task));
         break;
      case VK_SHADER_STAGE_MESH_BIT_EXT:
         populate_mesh_prog_key(&key.mesh, device, NULL, state, stages);
         _mesa_blake3_update(&blake3_ctx, &key.mesh, sizeof(key.mesh));
         break;
      case VK_SHADER_STAGE_FRAGMENT_BIT:
         populate_wm_prog_key(&key.wm, device, NULL, state, stages);
         _mesa_blake3_update(&blake3_ctx, &key.wm, sizeof(key.wm));
         break;
      case VK_SHADER_STAGE_COMPUTE_BIT:
         populate_cs_prog_key(&key.cs, device, NULL, false);
         _mesa_blake3_update(&blake3_ctx, &key.cs, sizeof(key.cs));
         break;
      default:
         UNREACHABLE("Invalid stage");
      }
   }

   _mesa_blake3_final(&blake3_ctx, blake3_out);
}

static void
merge_tess_info(struct shader_info *tes_info,
                const struct shader_info *tcs_info)
{
   /* The Vulkan 1.0.38 spec, section 21.1 Tessellator says:
    *
    *    "PointMode. Controls generation of points rather than triangles
    *     or lines. This functionality defaults to disabled, and is
    *     enabled if either shader stage includes the execution mode.
    *
    * and about Triangles, Quads, IsoLines, VertexOrderCw, VertexOrderCcw,
    * PointMode, SpacingEqual, SpacingFractionalEven, SpacingFractionalOdd,
    * and OutputVertices, it says:
    *
    *    "One mode must be set in at least one of the tessellation
    *     shader stages."
    *
    * So, the fields can be set in either the TCS or TES, but they must
    * agree if set in both.  Our backend looks at TES, so bitwise-or in
    * the values from the TCS.
    */
   assert(tcs_info->tess.tcs_vertices_out == 0 ||
          tes_info->tess.tcs_vertices_out == 0 ||
          tcs_info->tess.tcs_vertices_out == tes_info->tess.tcs_vertices_out);
   tes_info->tess.tcs_vertices_out |= tcs_info->tess.tcs_vertices_out;

   assert(tcs_info->tess.spacing == TESS_SPACING_UNSPECIFIED ||
          tes_info->tess.spacing == TESS_SPACING_UNSPECIFIED ||
          tcs_info->tess.spacing == tes_info->tess.spacing);
   tes_info->tess.spacing |= tcs_info->tess.spacing;

   assert(tcs_info->tess._primitive_mode == 0 ||
          tes_info->tess._primitive_mode == 0 ||
          tcs_info->tess._primitive_mode == tes_info->tess._primitive_mode);
   tes_info->tess._primitive_mode |= tcs_info->tess._primitive_mode;
   tes_info->tess.ccw |= tcs_info->tess.ccw;
   tes_info->tess.point_mode |= tcs_info->tess.point_mode;
}

static void
anv_shader_link_tcs(const struct brw_compiler *compiler,
                    struct brw_tcs_prog_key *key,
                    struct vk_shader_compile_info *tcs_stage,
                    struct vk_shader_compile_info *tes_stage)
{
   assert(tes_stage && tes_stage->stage == MESA_SHADER_TESS_EVAL);

   brw_nir_link_shaders(compiler, tcs_stage->nir, tes_stage->nir);

   nir_lower_patch_vertices(tes_stage->nir,
                            tcs_stage->nir->info.tess.tcs_vertices_out,
                            NULL);

   /* Copy TCS info into the TES info */
   merge_tess_info(&tes_stage->nir->info, &tcs_stage->nir->info);

   /* Whacking the key after cache lookup is a bit sketchy, but all of
    * this comes from the SPIR-V, which is part of the hash used for the
    * pipeline cache.  So it should be safe.
    */
   key->_tes_primitive_mode = tes_stage->nir->info.tess._primitive_mode;
}

static void
anv_shader_link(const struct brw_compiler *compiler,
                struct vk_shader_compile_info *prev_stage,
                struct vk_shader_compile_info *next_stage)
{
   brw_nir_link_shaders(compiler, prev_stage->nir, next_stage->nir);
}

static const struct vk_ycbcr_conversion_state *
lookup_ycbcr_conversion(const void *_stage, uint32_t set,
                        uint32_t binding, uint32_t array_index)
{
   const struct vk_shader_compile_info *stage = _stage;

   assert(set < MAX_SETS);
   const struct anv_descriptor_set_layout *set_layout =
      container_of(stage->set_layouts[set],
                   struct anv_descriptor_set_layout, vk);

   assert(binding < set_layout->binding_count);
   const struct anv_descriptor_set_binding_layout *bind_layout =
      &set_layout->binding[binding];

   if (bind_layout->samplers == NULL)
      return NULL;

   array_index = MIN2(array_index, bind_layout->array_size - 1);

   const struct anv_descriptor_set_layout_sampler *sampler =
      &bind_layout->samplers[array_index];

   return sampler->has_ycbcr_conversion ?
          &sampler->ycbcr_conversion_state : NULL;
}

static void
anv_fixup_subgroup_size(struct anv_device *device, nir_shader *shader)
{
   struct shader_info *info = &shader->info;
   const struct anv_instance *instance = device->physical->instance;

   if (!mesa_shader_stage_uses_workgroup(info->stage))
      return;

   unsigned local_size = info->workgroup_size[0] *
                         info->workgroup_size[1] *
                         info->workgroup_size[2];

   /* Games don't always request full subgroups when they should,
    * which can cause bugs, as they may expect bigger size of the
    * subgroup than we choose for the execution.
    */
   if (instance->assume_full_subgroups &&
       info->uses_wide_subgroup_intrinsics &&
       info->api_subgroup_size == BRW_SUBGROUP_SIZE &&
       local_size &&
       local_size % BRW_SUBGROUP_SIZE == 0) {
      info->max_subgroup_size = BRW_SUBGROUP_SIZE;
      info->min_subgroup_size = BRW_SUBGROUP_SIZE;
   }

   if (instance->assume_full_subgroups_with_barrier &&
       info->stage == MESA_SHADER_COMPUTE &&
       device->info->verx10 <= 125 &&
       info->uses_control_barrier &&
       info->min_subgroup_size != info->max_subgroup_size &&
       local_size &&
       local_size % BRW_SUBGROUP_SIZE == 0) {
      info->max_subgroup_size = BRW_SUBGROUP_SIZE;
      info->min_subgroup_size = BRW_SUBGROUP_SIZE;
   }

   /* Similarly, sometimes games rely on the implicit synchronization of
    * the shared memory accesses, and choosing smaller subgroups than the game
    * expects will cause bugs. */
   if (instance->assume_full_subgroups_with_shared_memory &&
       info->shared_size > 0 &&
       info->min_subgroup_size != info->max_subgroup_size &&
       local_size &&
       local_size % BRW_SUBGROUP_SIZE == 0) {
      info->max_subgroup_size = BRW_SUBGROUP_SIZE;
      info->min_subgroup_size = BRW_SUBGROUP_SIZE;
   }

   /* Cooperative matrix extension requires that all invocations in a subgroup
    * be active. As a result, when the application does not request a specific
    * subgroup size, we must use SIMD32.
    */
   if (info->stage == MESA_SHADER_COMPUTE && info->cs.has_cooperative_matrix &&
       info->max_subgroup_size > info->min_subgroup_size) {
      info->api_subgroup_size = info->max_subgroup_size;
      info->min_subgroup_size = info->max_subgroup_size;
   }

   /* Only promote to SIMD32 if the max allows it. */
   if (info->max_subgroup_size >= BRW_SUBGROUP_SIZE &&
       info->min_subgroup_size != info->max_subgroup_size &&
       info->uses_wide_subgroup_intrinsics &&
       nir_shader_intrinsics_pass(shader,
                                  detect_simd32_shuffle,
                                  nir_metadata_all,
                                  NULL)) {
      info->max_subgroup_size = BRW_SUBGROUP_SIZE;
      info->min_subgroup_size = BRW_SUBGROUP_SIZE;
   }
}

static void
anv_shader_compile_vs(struct anv_device *device,
                      void *mem_ctx,
                      struct anv_shader_data *shader_data,
                      char **error_str)
{
   const struct brw_compiler *compiler = device->physical->compiler;
   nir_shader *nir = shader_data->info->nir;

   shader_data->num_stats = 1;

   struct brw_compile_vs_params params = {
      .base = {
         .nir = nir,
         .stats = shader_data->stats,
         .log_data = device,
         .mem_ctx = mem_ctx,
         .source_hash = shader_data->source_hash,
         .archiver = shader_data->archiver,
      },
      .key = &shader_data->key.vs,
      .prog_data = &shader_data->prog_data.vs,
   };

   shader_data->code = (void *)brw_compile_vs(compiler, &params);
   *error_str = params.base.error_str;
}

static void
anv_shader_compile_tcs(struct anv_device *device,
                       void *mem_ctx,
                       struct anv_shader_data *shader_data,
                       char **error_str)
{
   const struct brw_compiler *compiler = device->physical->compiler;
   nir_shader *nir = shader_data->info->nir;

   shader_data->key.tcs.outputs_written = nir->info.outputs_written;
   shader_data->key.tcs.patch_outputs_written = nir->info.patch_outputs_written;

   shader_data->num_stats = 1;

   struct brw_compile_tcs_params params = {
      .base = {
         .nir = nir,
         .stats = shader_data->stats,
         .log_data = device,
         .mem_ctx = mem_ctx,
         .source_hash = shader_data->source_hash,
         .archiver = shader_data->archiver,
      },
      .key = &shader_data->key.tcs,
      .prog_data = &shader_data->prog_data.tcs,
   };

   shader_data->code = (void *)brw_compile_tcs(compiler, &params);
   *error_str = params.base.error_str;
}

static void
anv_shader_compile_tes(struct anv_device *device,
                       void *mem_ctx,
                       struct anv_shader_data *tes_shader_data,
                       struct anv_shader_data *tcs_shader_data,
                       char **error_str)
{
   const struct brw_compiler *compiler = device->physical->compiler;
   nir_shader *nir = tes_shader_data->info->nir;

   if (tcs_shader_data) {
      tes_shader_data->key.tes.inputs_read =
         tcs_shader_data->info->nir->info.outputs_written;
      tes_shader_data->key.tes.patch_inputs_read =
         tcs_shader_data->info->nir->info.patch_outputs_written;
   }

   tes_shader_data->num_stats = 1;

   struct brw_compile_tes_params params = {
      .base = {
         .nir = nir,
         .stats = tes_shader_data->stats,
         .log_data = device,
         .mem_ctx = mem_ctx,
         .source_hash = tes_shader_data->source_hash,
         .archiver = tes_shader_data->archiver,
      },
      .key = &tes_shader_data->key.tes,
      .prog_data = &tes_shader_data->prog_data.tes,
      .input_vue_map = tcs_shader_data ?
                       &tcs_shader_data->prog_data.tcs.base.vue_map : NULL,
   };

   tes_shader_data->code = (void *)brw_compile_tes(compiler, &params);
   *error_str = params.base.error_str;
}

static void
anv_shader_compile_gs(struct anv_device *device,
                      void *mem_ctx,
                      struct anv_shader_data *shader_data,
                      char **error_str)
{
   const struct brw_compiler *compiler = device->physical->compiler;
   nir_shader *nir = shader_data->info->nir;

   shader_data->num_stats = 1;

   struct brw_compile_gs_params params = {
      .base = {
         .nir = nir,
         .stats = shader_data->stats,
         .log_data = device,
         .mem_ctx = mem_ctx,
         .source_hash = shader_data->source_hash,
         .archiver = shader_data->archiver,
      },
      .key = &shader_data->key.gs,
      .prog_data = &shader_data->prog_data.gs,
   };

   shader_data->code = (void *)brw_compile_gs(compiler, &params);
   *error_str = params.base.error_str;
}

static void
anv_shader_compile_task(struct anv_device *device,
                        void *mem_ctx,
                        struct anv_shader_data *shader_data,
                        char **error_str)
{
   const struct brw_compiler *compiler = device->physical->compiler;
   nir_shader *nir = shader_data->info->nir;

   shader_data->num_stats = 1;

   struct brw_compile_task_params params = {
      .base = {
         .nir = nir,
         .stats = shader_data->stats,
         .log_data = device,
         .mem_ctx = mem_ctx,
         .source_hash = shader_data->source_hash,
         .archiver = shader_data->archiver,
      },
      .key = &shader_data->key.task,
      .prog_data = &shader_data->prog_data.task,
   };

   shader_data->code = (void *)brw_compile_task(compiler, &params);
   *error_str = params.base.error_str;
}

static nir_def *
mesh_load_provoking_vertex(nir_builder *b, void *data)
{
   return nir_load_inline_data_intel(
      b, 1, 32,
      .base = ANV_INLINE_PARAM_MESH_PROVOKING_VERTEX);
}

static void
anv_shader_compile_mesh(struct anv_device *device,
                        void *mem_ctx,
                        struct anv_shader_data *mesh_shader_data,
                        struct anv_shader_data *task_shader_data,
                        char **error_str)
{
   const struct brw_compiler *compiler = device->physical->compiler;
   nir_shader *nir = mesh_shader_data->info->nir;

   mesh_shader_data->num_stats = 1;

   struct brw_compile_mesh_params params = {
      .base = {
         .nir = nir,
         .stats = mesh_shader_data->stats,
         .log_data = device,
         .mem_ctx = mem_ctx,
         .source_hash = mesh_shader_data->source_hash,
         .archiver = mesh_shader_data->archiver,
      },
      .key = &mesh_shader_data->key.mesh,
      .prog_data = &mesh_shader_data->prog_data.mesh,
      .tue_map = task_shader_data ?
                 &task_shader_data->prog_data.task.map :
                 NULL,
      .load_provoking_vertex = mesh_load_provoking_vertex,
   };

   mesh_shader_data->code = (void *)brw_compile_mesh(compiler, &params);
   *error_str = params.base.error_str;
}

static void
anv_shader_compile_fs(struct anv_device *device,
                      void *mem_ctx,
                      struct anv_shader_data *shader_data,
                      const struct vk_graphics_pipeline_state *state,
                      char **error_str)
{
   const struct brw_compiler *compiler = device->physical->compiler;
   nir_shader *nir = shader_data->info->nir;

   /* When using Primitive Replication for multiview, each view gets its own
    * position slot.
    */
   uint32_t pos_slots = shader_data->use_primitive_replication ?
      MAX2(1, util_bitcount(shader_data->key.base.view_mask)) : 1;

   struct intel_vue_map prev_vue_map;
   brw_compute_vue_map(compiler->devinfo,
                       &prev_vue_map,
                       nir->info.inputs_read,
                       nir->info.separate_shader,
                       pos_slots);

   struct brw_compile_fs_params params = {
      .base = {
         .nir = nir,
         .stats = shader_data->stats,
         .log_data = device,
         .mem_ctx = mem_ctx,
         .source_hash = shader_data->source_hash,
         .archiver = shader_data->archiver,
      },
      .key = &shader_data->key.wm,
      .prog_data = &shader_data->prog_data.wm,
      .mue_map = shader_data->mue_map,

      .allow_spilling = true,
      .max_polygons = UCHAR_MAX,
   };

   shader_data->code = (void *)brw_compile_fs(compiler, &params);
   *error_str = params.base.error_str;

   shader_data->num_stats = (uint32_t)!!shader_data->prog_data.wm.dispatch_multi +
                            (uint32_t)shader_data->prog_data.wm.dispatch_8 +
                            (uint32_t)shader_data->prog_data.wm.dispatch_16 +
                            (uint32_t)shader_data->prog_data.wm.dispatch_32;
   assert(shader_data->num_stats <= ARRAY_SIZE(shader_data->stats));

   /* Update the push constant padding range now that we know the amount of
    * per-primitive data delivered in the payload.
    */
   for (unsigned i = 0; i < ARRAY_SIZE(shader_data->bind_map.push_ranges); i++) {
      if (shader_data->bind_map.push_ranges[i].set == ANV_DESCRIPTOR_SET_PER_PRIM_PADDING) {
         shader_data->bind_map.push_ranges[i].length = MAX2(
            shader_data->prog_data.wm.num_per_primitive_inputs / 2,
            shader_data->bind_map.push_ranges[i].length);
         break;
      }
   }
}

static void
anv_shader_compile_cs(struct anv_device *device,
                      void *mem_ctx,
                      struct anv_shader_data *shader_data,
                      char **error_str)
{
   const struct brw_compiler *compiler = device->physical->compiler;
   nir_shader *nir = shader_data->info->nir;

   shader_data->num_stats = 1;

   struct brw_compile_cs_params params = {
      .base = {
         .nir = nir,
         .stats = shader_data->stats,
         .log_data = device,
         .mem_ctx = mem_ctx,
         .source_hash = shader_data->source_hash,
         .archiver = shader_data->archiver,
      },
      .key = &shader_data->key.cs,
      .prog_data = &shader_data->prog_data.cs,
   };

   shader_data->code = (void *)brw_compile_cs(compiler, &params);
   *error_str = params.base.error_str;
}

static bool
should_remat_cb(nir_instr *instr, void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   return nir_instr_as_intrinsic(instr)->intrinsic == nir_intrinsic_resource_intel;
}

static void
anv_shader_compile_bs(struct anv_device *device,
                      void *mem_ctx,
                      struct anv_shader_data *shader_data,
                      char **error_str)
{
   const struct brw_compiler *compiler = device->physical->compiler;
   nir_shader *nir = shader_data->info->nir;
   const struct intel_device_info *devinfo = compiler->devinfo;

   struct brw_nir_lower_shader_calls_state lowering_state = {
      .devinfo = devinfo,
      .key = &shader_data->key.bs,
   };

   nir_shader **resume_shaders = NULL;
   uint32_t num_resume_shaders = 0;
   if (nir->info.stage != MESA_SHADER_COMPUTE) {
      const nir_lower_shader_calls_options opts = {
         .address_format = nir_address_format_64bit_global,
         .stack_alignment = BRW_BTD_STACK_ALIGN,
         .localized_loads = true,
         .vectorizer_callback = brw_nir_should_vectorize_mem,
         .vectorizer_data = NULL,
         .should_remat_callback = should_remat_cb,
      };

      NIR_PASS(_, nir, brw_nir_lower_rt_intrinsics_pre_trace);
      NIR_PASS(_, nir, nir_lower_shader_calls, &opts,
               &resume_shaders, &num_resume_shaders, mem_ctx);
      NIR_PASS(_, nir, brw_nir_lower_shader_calls, &lowering_state);
      NIR_PASS(_, nir, brw_nir_lower_rt_intrinsics,
               &shader_data->key.base, devinfo);
   }

   for (unsigned i = 0; i < num_resume_shaders; i++) {
      NIR_PASS(_, resume_shaders[i], brw_nir_lower_shader_calls,
               &lowering_state);
      NIR_PASS(_, resume_shaders[i], brw_nir_lower_rt_intrinsics,
               &shader_data->key.base, devinfo);
   }

   shader_data->num_stats = 1;

   struct brw_compile_bs_params params = {
      .base = {
         .nir = nir,
         .stats = shader_data->stats,
         .log_data = device,
         .mem_ctx = mem_ctx,
         .source_hash = shader_data->source_hash,
         .archiver = shader_data->archiver,
      },
      .key = &shader_data->key.bs,
      .prog_data = &shader_data->prog_data.bs,
      .num_resume_shaders = num_resume_shaders,
      .resume_shaders = resume_shaders,
   };

   shader_data->code = (void *)brw_compile_bs(compiler, &params);
   *error_str = params.base.error_str;
}

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

static void
anv_shader_compute_fragment_rts(const struct brw_compiler *compiler,
                                const struct vk_graphics_pipeline_state *state,
                                struct anv_shader_data *shader_data)
{
   assert(shader_data->bind_map.surface_count == 0);

   nir_shader *nir = shader_data->info->nir;
   const uint64_t rt_mask = nir->info.outputs_written >> FRAG_RESULT_DATA0;
   const unsigned num_rts = util_last_bit64(rt_mask);
   struct anv_pipeline_binding rt_bindings[MAX_RTS];

   shader_data->key.wm.color_outputs_valid = rt_mask & rp_color_mask(state);
   shader_data->key.wm.nr_color_regions =
      util_last_bit(shader_data->key.wm.color_outputs_valid);

   if (num_rts > 0) {
      for (unsigned rt = 0; rt < num_rts; rt++) {
         if (nir->info.outputs_written & BITFIELD_BIT(FRAG_RESULT_DATA0 + rt)) {
            rt_bindings[rt] = (struct anv_pipeline_binding) {
               .set = ANV_DESCRIPTOR_SET_COLOR_ATTACHMENTS,
               .index = rt,
               .binding = UINT32_MAX,
            };
         } else {
            /* Setup a null render target */
            rt_bindings[rt] = (struct anv_pipeline_binding) {
               .set = ANV_DESCRIPTOR_SET_COLOR_ATTACHMENTS,
               .index = ANV_COLOR_OUTPUT_UNUSED,
               .binding = UINT32_MAX,
            };
         }
      }
      shader_data->bind_map.surface_count = num_rts;
   } else if (brw_nir_fs_needs_null_rt(
                 compiler->devinfo, nir,
                 shader_data->key.wm.alpha_to_coverage != INTEL_NEVER)) {
      /* Setup a null render target */
      rt_bindings[0] = (struct anv_pipeline_binding) {
         .set = ANV_DESCRIPTOR_SET_COLOR_ATTACHMENTS,
         .index = ANV_COLOR_OUTPUT_DISABLED,
         .binding = UINT32_MAX,
      };
      shader_data->bind_map.surface_count = 1;
   }

   typed_memcpy(shader_data->bind_map.surface_to_descriptor,
                rt_bindings, shader_data->bind_map.surface_count);
}

static bool
accept_64bit_atomic_cb(const nir_intrinsic_instr *intrin, const void *data)
{
   return (intrin->intrinsic == nir_intrinsic_image_atomic ||
           intrin->intrinsic == nir_intrinsic_image_atomic_swap ||
           intrin->intrinsic == nir_intrinsic_image_deref_atomic ||
           intrin->intrinsic == nir_intrinsic_image_deref_atomic_swap) &&
          intrin->def.bit_size == 64;
}

static bool
lower_non_tg4_non_uniform_offsets(const nir_tex_instr *tex,
                                  unsigned index, void *data)
{
   /* HW cannot deal with divergent surfaces/samplers */
   if (tex->src[index].src_type == nir_tex_src_texture_offset ||
       tex->src[index].src_type == nir_tex_src_texture_handle ||
       tex->src[index].src_type == nir_tex_src_sampler_offset ||
       tex->src[index].src_type == nir_tex_src_sampler_handle)
      return true;

   if (tex->src[index].src_type == nir_tex_src_offset) {
      /* HW can deal with TG4 divergent offsets only */
      return tex->op != nir_texop_tg4;
   }

   return false;
}

static void
fixup_large_workgroup_image_coherency(nir_shader *nir)
{
   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

            if (intr->intrinsic != nir_intrinsic_image_deref_store ||
                nir_intrinsic_image_dim(intr) != GLSL_SAMPLER_DIM_3D)
               continue;

            /* We have found image store access to 3D. */
            nir_deref_instr *array_deref = nir_src_as_deref(intr->src[0]);
            if (array_deref->deref_type != nir_deref_type_array)
               continue;

            nir_alu_instr *alu = nir_src_as_alu(intr->src[1]);
            if (!alu || !nir_op_is_vec(alu->op))
               return;

            /* Check if any src is from @load_local_invocation_id. */
            for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; i++) {
               nir_instr *parent = nir_def_instr(alu->src[i].src.ssa);
               if (parent->type != nir_instr_type_intrinsic)
                  continue;

               nir_intrinsic_instr *parent_intr = nir_instr_as_intrinsic(parent);
               if (parent_intr->intrinsic !=
                   nir_intrinsic_load_local_invocation_id)
                  continue;

               /* Found a match, change image access qualifier coherent. */
               nir_deref_instr *parent_deref =
                  nir_src_as_deref(array_deref->parent);
               parent_deref->var->data.access = ACCESS_COHERENT;
               return;
            }
         }  /* instr */
      } /* block */
   } /* func */
}

static void
anv_shader_lower_nir(struct anv_device *device,
                     void *mem_ctx,
                     const struct vk_graphics_pipeline_state *state,
                     struct anv_shader_data *shader_data)
{
   const struct anv_physical_device *pdevice = device->physical;
   const struct brw_compiler *compiler = pdevice->compiler;
   struct anv_descriptor_set_layout * const *set_layouts =
      (struct anv_descriptor_set_layout * const *) shader_data->info->set_layouts;
   const uint32_t set_layout_count = shader_data->info->set_layout_count;

   nir_shader *nir = shader_data->info->nir;

   /* Workaround for apps that need fp64 support */
   if (device->fp64_nir) {
      NIR_PASS(_, nir, nir_lower_doubles, device->fp64_nir,
               nir->options->lower_doubles_options);

      bool fp_conv = false;
      NIR_PASS(fp_conv, nir, nir_lower_int64_float_conversions);
      if (fp_conv) {
         NIR_PASS(_, nir, nir_opt_algebraic);
         NIR_PASS(_, nir, nir_lower_doubles, device->fp64_nir,
                  nir->options->lower_doubles_options);
      }
   }

   if (nir->info.stage == MESA_SHADER_COMPUTE &&
       pdevice->instance->large_workgroup_non_coherent_image_workaround) {
      const unsigned local_size = nir->info.workgroup_size[0] *
                                  nir->info.workgroup_size[1] *
                                  nir->info.workgroup_size[2];
      if (local_size == 64)
         fixup_large_workgroup_image_coherency(nir);
   }

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS(_, nir, nir_lower_wpos_center);
      NIR_PASS(_, nir, nir_lower_input_attachments,
               &(nir_input_attachment_options) { });
   }

   if (nir->info.stage == MESA_SHADER_COMPUTE &&
       shader_data->key.cs.lower_unaligned_dispatch) {
      NIR_PASS(_, nir, anv_nir_lower_unaligned_dispatch);
      /* anv_nir_lower_unaligned_dispatch pass uses nir_jump_return that we
       * need to lower it.
       */
      NIR_PASS(_, nir, nir_lower_returns);
      /* Lower load_base_workgroup_id inserted by unaligned_dispatch */
      nir_lower_compute_system_values_options lower_csv_options = {
         .has_base_workgroup_id = true,
      };
      NIR_PASS(_, nir, nir_lower_compute_system_values, &lower_csv_options);
   }

   if (mesa_shader_stage_is_mesh(nir->info.stage)) {
      nir_lower_compute_system_values_options options = {
         .lower_workgroup_id_to_index = true,
         /* nir_lower_idiv generates expensive code */
         .shortcut_1d_workgroup_id = compiler->devinfo->verx10 >= 125,
      };

      NIR_PASS(_, nir, nir_lower_compute_system_values, &options);
   }

   NIR_PASS(_, nir, nir_vk_lower_ycbcr_tex, lookup_ycbcr_conversion, shader_data->info);

   if (nir->info.stage <= MESA_SHADER_FRAGMENT) {
      NIR_PASS(_, nir, anv_nir_lower_multiview,
               shader_data->key.base.view_mask,
               shader_data->use_primitive_replication);
   }

   if (nir->info.stage == MESA_SHADER_COMPUTE &&
       nir->info.cs.has_cooperative_matrix) {
      anv_fixup_subgroup_size(device, nir);
      NIR_PASS(_, nir, brw_nir_lower_cmat, nir->info.api_subgroup_size);
      NIR_PASS(_, nir, nir_lower_indirect_derefs_to_if_else_trees,
               nir_var_function_temp, 16);
   }

   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   /* Apply lowering for 64bit atomics pre-Xe2 */
   const bool lower_64bit_atomics = compiler->devinfo->ver < 20;

   if (lower_64bit_atomics) {
      /* Ensure robustness, do this before brw_nir_lower_storage_image so that
       * added image size intrinsics for bounds checkings are properly lowered
       * for cube images.
       */
      NIR_PASS(_, nir, nir_lower_robust_access,
               accept_64bit_atomic_cb, NULL);
   }

   NIR_PASS(_, nir, brw_nir_lower_storage_image, compiler,
            &(struct brw_nir_lower_storage_image_opts) {
               /* Anv only supports Gfx9+ which has better defined typed read
                * behavior. It allows us to only have to care about lowering
                * loads.
                */
               .lower_loads = true,
               .lower_stores_64bit = true,
               .lower_loads_without_formats =
                  pdevice->instance->emulate_read_without_format,
            });

   if (lower_64bit_atomics) {
      /* Switch from image to global */
      NIR_PASS(_, nir, nir_lower_image_atomics_to_global,
               accept_64bit_atomic_cb, NULL);

      /* Detile for global */
      NIR_PASS(_, nir, brw_nir_lower_texel_address, compiler->devinfo,
               pdevice->isl_dev.shader_tiling);
   }

   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_global,
            nir_address_format_64bit_global);
   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_push_const,
            nir_address_format_32bit_offset);

   NIR_PASS(_, nir, brw_nir_lower_ray_queries, &pdevice->info);

   shader_data->push_desc_info.used_descriptors =
      anv_nir_compute_used_push_descriptors(
         nir, set_layouts, set_layout_count);

   /* Need to have render targets placed first in the bind_map */
   if (nir->info.stage == MESA_SHADER_FRAGMENT)
      anv_shader_compute_fragment_rts(compiler, state, shader_data);


   uint32_t dynamic_descriptors_offset = 0;
   uint32_t dynamic_descriptors_offsets[MAX_SETS] = {};
   for (uint32_t i = 0; i < set_layout_count; i++) {
      dynamic_descriptors_offsets[i] = dynamic_descriptors_offset;
      dynamic_descriptors_offset += set_layouts[i] != NULL ?
         set_layouts[i]->vk.dynamic_descriptor_count : 0;
   }

   /* Apply the actual pipeline layout to UBOs, SSBOs, and textures */
   NIR_PASS(_, nir, anv_nir_apply_pipeline_layout,
               pdevice, shader_data->key.base.robust_flags,
               set_layouts, set_layout_count,
               (shader_data->info->flags &
                VK_SHADER_CREATE_INDEPENDENT_SETS_BIT_MESA) ? NULL:
               dynamic_descriptors_offsets,
               &shader_data->bind_map, &shader_data->push_map, mem_ctx);

   NIR_PASS(_, nir, anv_nir_lower_driver_values, pdevice);

   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_ubo,
            anv_nir_ubo_addr_format(pdevice, shader_data->key.base.robust_flags));
   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_ssbo,
            anv_nir_ssbo_addr_format(pdevice, shader_data->key.base.robust_flags));

   /* First run copy-prop to get rid of all of the vec() that address
    * calculations often create and then constant-fold so that, when we
    * get to anv_nir_lower_ubo_loads, we can detect constant offsets.
    */
   bool progress;
   do {
      progress = false;
      NIR_PASS(progress, nir, nir_opt_algebraic);
      NIR_PASS(progress, nir, nir_opt_copy_prop);
      NIR_PASS(progress, nir, nir_opt_constant_folding);
      NIR_PASS(progress, nir, nir_opt_dce);
   } while (progress);

   /* Required for nir_divergence_analysis() which is needed for
    * anv_nir_lower_ubo_loads.
    */
   NIR_PASS(_, nir, nir_convert_to_lcssa, true, true);
   nir_divergence_analysis(nir);

   NIR_PASS(_, nir, anv_nir_lower_ubo_loads);

   NIR_PASS(_, nir, nir_opt_remove_phis);

   const bool lower_non_uniform_texture_offsets = device->info->ver < 20;

   enum nir_lower_non_uniform_access_type lower_non_uniform_access_types =
      nir_lower_non_uniform_texture_access |
      nir_lower_non_uniform_image_access |
      nir_lower_non_uniform_get_ssbo_size |
      (lower_non_uniform_texture_offsets ?
       nir_lower_non_uniform_texture_offset_access : 0);

   /* Pre-Xe2 platforms don't have native support for dynamic programmable
    * offsets. Since support includes non-uniform programmable offsets, we
    * need to lower those texture messages in the same way we lower
    * non-uniform texture/sampler handles.
    */
   if (lower_non_uniform_texture_offsets) {
      nir_foreach_function_impl(impl, nir)
         nir_metadata_require(impl, nir_metadata_divergence);
   }

   /* In practice, most shaders do not have non-uniform-qualified
    * accesses (see
    * https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/17558#note_1475069)
    * thus a cheaper and likely to fail check is run first.
    */
   if (nir_has_non_uniform_access(nir, lower_non_uniform_access_types)) {
      NIR_PASS(_, nir, nir_opt_non_uniform_access);

      /* We don't support non-uniform UBOs and non-uniform SSBO access is
      * handled naturally by falling back to A64 messages.
      */
      NIR_PASS(_, nir, nir_lower_non_uniform_access,
               &(nir_lower_non_uniform_access_options) {
                  .types = lower_non_uniform_access_types,
                  .tex_src_callback = lower_non_tg4_non_uniform_offsets,
                  .callback = NULL,
               });

      NIR_PASS(_, nir, intel_nir_lower_non_uniform_resource_intel);
      NIR_PASS(_, nir, intel_nir_cleanup_resource_intel);
      NIR_PASS(_, nir, nir_opt_dce);
   }

   NIR_PASS(_, nir, anv_nir_update_resource_intel_block);

   NIR_PASS(_, nir, anv_nir_compute_push_layout,
               pdevice, shader_data->key.base.robust_flags,
               &(struct anv_nir_push_layout_info) {
                  .separate_tessellation = (nir->info.stage == MESA_SHADER_TESS_CTRL &&
                                            shader_data->key.tcs.separate_tess_vue_layout) ||
                                           (nir->info.stage == MESA_SHADER_TESS_EVAL &&
                                            shader_data->key.tes.separate_tess_vue_layout),
                  .fragment_dynamic      = nir->info.stage == MESA_SHADER_FRAGMENT &&
                                           brw_wm_prog_key_is_dynamic(&shader_data->key.wm),
                  .mesh_dynamic          = nir->info.stage == MESA_SHADER_FRAGMENT &&
                                           shader_data->key.wm.mesh_input == INTEL_SOMETIMES,
               },
               &shader_data->key.base,
               &shader_data->prog_data.base,
               &shader_data->bind_map, &shader_data->push_map,
               mem_ctx);

   NIR_PASS(_, nir, anv_nir_lower_resource_intel, pdevice,
               shader_data->bind_map.layout_type);

   if (mesa_shader_stage_uses_workgroup(nir->info.stage)) {
      NIR_PASS(_, nir, nir_lower_vars_to_explicit_types,
               nir_var_mem_shared, shared_type_info);

      NIR_PASS(_, nir, nir_lower_explicit_io,
               nir_var_mem_shared, nir_address_format_32bit_offset);

      if (nir->info.zero_initialize_shared_memory &&
          nir->info.shared_size > 0) {
         /* The effective Shared Local Memory size is at least 1024 bytes and
          * is always rounded to a power of two, so it is OK to align the size
          * used by the shader to chunk_size -- which does simplify the logic.
          */
         const unsigned chunk_size = 16;
         const unsigned shared_size = align(nir->info.shared_size, chunk_size);
         assert(shared_size <=
                intel_compute_slm_calculate_size(compiler->devinfo->ver,
                                                 nir->info.shared_size));

         NIR_PASS(_, nir, nir_zero_initialize_shared_memory,
                  shared_size, chunk_size);
      }
   }

   if (mesa_shader_stage_is_compute(nir->info.stage) ||
       mesa_shader_stage_is_mesh(nir->info.stage)) {
      NIR_PASS(_, nir, brw_nir_lower_cs_intrinsics, compiler->devinfo,
               &shader_data->prog_data.cs);
   }

   shader_data->push_desc_info.push_set_buffer =
      anv_nir_loads_push_desc_buffer(
         nir, set_layouts, set_layout_count, &shader_data->bind_map);
   shader_data->push_desc_info.fully_promoted_ubo_descriptors =
      anv_nir_push_desc_ubo_fully_promoted(
         nir, set_layouts, set_layout_count, &shader_data->bind_map);
}

static uint32_t
sets_layout_embedded_sampler_count(const struct vk_shader_compile_info *info)
{
   uint32_t count = 0;

   for (uint32_t s = 0; s < info->set_layout_count; s++) {
      if (info->set_layouts[s] == NULL)
         continue;

      const struct anv_descriptor_set_layout *layout =
         (const struct anv_descriptor_set_layout *) info->set_layouts[s];
      count += layout->embedded_sampler_count;
   }

   return count;
}

static void
anv_shaders_pre_lower_gfx(struct anv_device *device,
                          struct anv_shader_data *shaders_data,
                          uint32_t shader_count,
                          const struct vk_graphics_pipeline_state *state,
                          void *mem_ctx)
{
   const struct intel_device_info *devinfo = device->info;
   const struct brw_compiler *compiler = device->physical->compiler;

   /* Walk backwards to link */
   struct anv_shader_data *next_stage = NULL;
   for (int s = shader_count - 1; s >= 0; s--) {
      struct anv_shader_data *shader_data = &shaders_data[s];
      struct vk_shader_compile_info *info = shader_data->info;

      if (next_stage == NULL) {
         next_stage = shader_data;
         continue;
      }

      switch (info->stage) {
      case MESA_SHADER_VERTEX:
      case MESA_SHADER_TESS_EVAL:
      case MESA_SHADER_TASK:
      case MESA_SHADER_GEOMETRY:
         anv_shader_link(compiler, info, next_stage->info);
         break;
      case MESA_SHADER_TESS_CTRL:
         anv_shader_link_tcs(compiler,
                             &shader_data->key.tcs,
                             info, next_stage->info);
         break;
      case MESA_SHADER_MESH:
         anv_shader_link(compiler, info, next_stage->info);
         next_stage->mue_map = &shader_data->prog_data.mesh.map;
         break;
      default:
         UNREACHABLE("Invalid graphics shader stage");
      }

      next_stage = shader_data;
   }

   bool use_primitive_replication = false;
   if (devinfo->ver >= 12 && shaders_data[0].key.base.view_mask != 0) {
      /* For some pipelines HW Primitive Replication can be used instead of
       * instancing to implement Multiview.  This depend on how viewIndex is
       * used in all the active shaders, so this check can't be done per
       * individual shaders.
       */
      nir_shader *shaders[ANV_GRAPHICS_SHADER_STAGE_COUNT] = {};
      VkShaderStageFlags vk_stages = 0;
      for (unsigned s = 0; s < shader_count; s++) {
         struct anv_shader_data *shader_data = &shaders_data[s];
         shaders[shader_data->info->stage] = shader_data->info->nir;
         vk_stages |= mesa_to_vk_shader_stage(shader_data->info->stage);
      }

      use_primitive_replication =
         anv_check_for_primitive_replication(device, vk_stages, shaders,
                                             shaders_data[0].key.base.view_mask);
   }

   for (uint32_t s = 0; s < shader_count; s++) {
      struct anv_shader_data *shader_data = &shaders_data[s];
      shader_data->use_primitive_replication = use_primitive_replication;
      shader_data->instance_multiplier =
         (shader_data->key.base.view_mask && !use_primitive_replication) ?
         util_bitcount(shader_data->key.base.view_mask) : 1;
   }
}

static void
anv_shaders_post_lower_gfx(struct anv_device *device,
                           struct anv_shader_data *shaders_data,
                           uint32_t shader_count,
                           const struct vk_graphics_pipeline_state *state)
{
   struct vk_shader_compile_info *prev_stage = NULL;
   for (uint32_t s = 0; s < shader_count; s++) {
      struct anv_shader_data *shader_data = &shaders_data[s];
      struct vk_shader_compile_info *info = shader_data->info;

      struct shader_info *cur_info = &shader_data->info->nir->info;

      if (prev_stage && info->stage < MESA_SHADER_FRAGMENT) {
         struct shader_info *prev_info = &prev_stage->nir->info;

         prev_info->outputs_written |= cur_info->inputs_read &
                  ~(VARYING_BIT_TESS_LEVEL_INNER | VARYING_BIT_TESS_LEVEL_OUTER);
         cur_info->inputs_read |= prev_info->outputs_written &
                  ~(VARYING_BIT_TESS_LEVEL_INNER | VARYING_BIT_TESS_LEVEL_OUTER);
         prev_info->patch_outputs_written |= cur_info->patch_inputs_read;
         cur_info->patch_inputs_read |= prev_info->patch_outputs_written;
      }

      prev_stage = info;
   }
}

static void
anv_shaders_post_lower_rt(struct anv_device *device,
                          struct anv_shader_data *shaders_data,
                          uint32_t shader_count)
{
   for (uint32_t s = 0; s < shader_count; s++) {
      struct anv_shader_data *shader_data = &shaders_data[s];
      nir_shader *nir = shader_data->info->nir;

      switch (nir->info.stage) {
      case MESA_SHADER_RAYGEN:
         brw_nir_lower_raygen(nir, device->info);
         break;

      case MESA_SHADER_ANY_HIT:
         brw_nir_lower_any_hit(nir, device->info);
         break;

      case MESA_SHADER_CLOSEST_HIT:
         brw_nir_lower_closest_hit(nir, device->info);
         break;

      case MESA_SHADER_MISS:
         brw_nir_lower_miss(nir, device->info);
         break;

      case MESA_SHADER_CALLABLE:
         brw_nir_lower_callable(nir, device->info);
         break;

      case MESA_SHADER_INTERSECTION:
         /* Nothing to do, we merge this into ANY_HIT */
         break;

      default:
         UNREACHABLE("invalid stage");
      }
   }
}

static VkShaderStageFlags
anv_shader_get_rt_group_linking(struct vk_physical_device *device,
                                VkShaderStageFlags stages)
{
   const VkShaderStageFlags any_hit_intersection =
      VK_SHADER_STAGE_ANY_HIT_BIT_KHR |
      VK_SHADER_STAGE_INTERSECTION_BIT_KHR;

   return (stages & any_hit_intersection) == any_hit_intersection ?
          any_hit_intersection : 0;
}

static void
anv_debug_archiver_init(void *mem_ctx, struct anv_shader_data *shaders_data,
                        uint32_t shader_count)
{
   /* A hash is used to identify the per stage archive file.  Just using the
    * single stage hash/key is sufficient if it is not linked.  If shaders
    * are linked together, also include a combined hash of all stages to
    * distinguish from the not linked case.
    */
   unsigned char linked_hash[SHA1_DIGEST_LENGTH];
   if (shader_count > 1) {
      struct mesa_sha1 ctx;
      _mesa_sha1_init(&ctx);

      for (uint32_t s = 0; s < shader_count; s++) {
         struct anv_shader_data *shader_data = &shaders_data[s];
         struct vk_shader_compile_info *info = shader_data->info;
         _mesa_sha1_update(&ctx, info->nir->info.source_blake3, BLAKE3_OUT_LEN);
         _mesa_sha1_update(&ctx, &shader_data->key, shader_data->key_size);
      }
      _mesa_sha1_final(&ctx, linked_hash);
   }

   for (uint32_t s = 0; s < shader_count; s++) {
      struct anv_shader_data *shader_data = &shaders_data[s];
      struct vk_shader_compile_info *info = shader_data->info;

      char name[SHA1_DIGEST_STRING_LENGTH + 4] = {};
      {
         struct mesa_sha1 ctx;
         unsigned char hash[SHA1_DIGEST_LENGTH];
         _mesa_sha1_init(&ctx);
         _mesa_sha1_update(&ctx, info->nir->info.source_blake3, BLAKE3_OUT_LEN);
         _mesa_sha1_update(&ctx, &shader_data->key, shader_data->key_size);
         if (shader_count > 1)
            _mesa_sha1_update(&ctx, linked_hash, SHA1_DIGEST_LENGTH);
         _mesa_sha1_final(&ctx, hash);

         _mesa_sha1_format(name, hash);
      }
      memcpy(&name[SHA1_DIGEST_STRING_LENGTH - 1], ".anv", 4);

      shader_data->archiver =
         debug_archiver_open(mem_ctx, name, PACKAGE_VERSION MESA_GIT_SHA1);

      if (shader_data->archiver) {
         debug_archiver_set_prefix(shader_data->archiver,
            _mesa_shader_stage_to_abbrev(info->stage));
      }
   }
}

static void
anv_debug_archiver_finish(struct anv_shader_data *shaders_data,
                          uint32_t shader_count)
{
   for (uint32_t s = 0; s < shader_count; s++) {
      struct anv_shader_data *shader_data = &shaders_data[s];
      debug_archiver_close(shader_data->archiver);
      shader_data->archiver = NULL;
   }
}

static VkResult
anv_shader_compile(struct vk_device *vk_device,
                   uint32_t shader_count,
                   struct vk_shader_compile_info *infos,
                   const struct vk_graphics_pipeline_state *state,
                   const struct vk_features *enabled_features,
                   const VkAllocationCallbacks* pAllocator,
                   struct vk_shader **shaders_out)
{
   struct anv_device *device =
      container_of(vk_device, struct anv_device, vk);
   VkResult result = VK_SUCCESS;

   for (uint32_t i = 0; i < shader_count; i++)
      shaders_out[i] = NULL;

   void *mem_ctx = ralloc_context(NULL);

   struct anv_shader_data *shaders_data =
      rzalloc_array(mem_ctx, struct anv_shader_data, shader_count);
   assert(shader_count < MAX2(ANV_GRAPHICS_SHADER_STAGE_COUNT,
                              ANV_RT_SHADER_STAGE_COUNT));

   /* Order the stages (no guarantee from the runtime) */
   struct vk_shader_compile_info *ordered_infos[MESA_SHADER_KERNEL] = { 0 };
   struct vk_shader **ordered_shaders_out[MESA_SHADER_KERNEL] = { 0 };
   VkShaderStageFlags stages = 0;
   for (uint32_t s = 0; s < shader_count; s++) {
      ordered_infos[infos[s].stage] = &infos[s];
      ordered_shaders_out[infos[s].stage] = &shaders_out[s];

      /* The runtime transfers the ownership of the NIR to us, so we need to
       * free it after compile.
       */
      ralloc_steal(mem_ctx, infos[s].nir);
      stages |= mesa_to_vk_shader_stage(infos[s].stage);
   }

   {
#define ADD_SHADER(_name)                                               \
   do {                                                                 \
      if (ordered_infos[MESA_SHADER_##_name]) {                         \
         shaders_data[remapped_index].info =                            \
            ordered_infos[MESA_SHADER_##_name];                         \
         shaders_data[remapped_index].shader_out =                      \
            ordered_shaders_out[MESA_SHADER_##_name];                   \
         remapped_index++;                                              \
      }                                                                 \
   } while (0)

      uint32_t remapped_index = 0;
      ADD_SHADER(COMPUTE);
      ADD_SHADER(VERTEX);
      ADD_SHADER(TESS_CTRL);
      ADD_SHADER(TESS_EVAL);
      ADD_SHADER(GEOMETRY);
      ADD_SHADER(TASK);
      ADD_SHADER(MESH);
      ADD_SHADER(FRAGMENT);
      ADD_SHADER(RAYGEN);
      ADD_SHADER(CLOSEST_HIT);
      ADD_SHADER(INTERSECTION);
      ADD_SHADER(ANY_HIT);
      ADD_SHADER(MISS);
      ADD_SHADER(CALLABLE);

#undef ADD_SHADER
   }

   /* From now on, don't use infos[] anymore. */

   for (uint32_t s = 0; s < shader_count; s++) {
      struct anv_shader_data *shader_data = &shaders_data[s];
      struct vk_shader_compile_info *info = shader_data->info;

      shader_data->source_hash = ((uint32_t*)info->nir->info.source_blake3)[0];

      for (uint32_t i = 0; i < info->set_layout_count; i++) {
         shader_data->dynamic_descriptors[i] =
            info->set_layouts[i] != NULL ?
            info->set_layouts[i]->dynamic_descriptor_count : 0;
      }

      shader_data->bind_map.layout_type =
         set_layouts_get_layout_type((struct anv_descriptor_set_layout * const *)info->set_layouts,
                                     info->set_layout_count);
      shader_data->bind_map.surface_to_descriptor =
         brw_shader_stage_requires_bindless_resources(info->stage) ? NULL :
         rzalloc_array(mem_ctx, struct anv_pipeline_binding, 256);
      shader_data->bind_map.sampler_to_descriptor =
         brw_shader_stage_requires_bindless_resources(info->stage) ? NULL :
         rzalloc_array(mem_ctx, struct anv_pipeline_binding, 256);
      shader_data->bind_map.embedded_sampler_to_binding =
         rzalloc_array(mem_ctx, struct anv_pipeline_embedded_sampler_binding,
                       sets_layout_embedded_sampler_count(info));

      shader_data->prog_data.base.stage = info->stage;

      switch (info->stage) {
      case MESA_SHADER_VERTEX:
         populate_vs_prog_key(&shader_data->key.vs, vk_device->physical,
                              info->robustness, state, stages);
         break;
      case MESA_SHADER_TESS_CTRL:
         populate_tcs_prog_key(&shader_data->key.tcs, vk_device->physical,
                               info->robustness, state, stages);
         break;
      case MESA_SHADER_TESS_EVAL:
         populate_tes_prog_key(&shader_data->key.tes, vk_device->physical,
                               info->robustness, state, stages);
         break;
      case MESA_SHADER_GEOMETRY:
         populate_gs_prog_key(&shader_data->key.gs, vk_device->physical,
                              info->robustness, state, stages);
         break;
      case MESA_SHADER_TASK:
         populate_task_prog_key(&shader_data->key.task, vk_device->physical,
                                info->robustness, state, stages);
         break;
      case MESA_SHADER_MESH:
         populate_mesh_prog_key(&shader_data->key.mesh, vk_device->physical,
                                info->robustness, state, stages);
         break;
      case MESA_SHADER_FRAGMENT:
         populate_wm_prog_key(&shader_data->key.wm, vk_device->physical,
                              info->robustness, state, stages);
         break;
      case MESA_SHADER_COMPUTE:
         populate_cs_prog_key(&shader_data->key.cs, vk_device->physical,
                              info->robustness,
                              info->flags & VK_SHADER_CREATE_UNALIGNED_DISPATCH_BIT_MESA);
         break;
      case MESA_SHADER_RAYGEN:
      case MESA_SHADER_ANY_HIT:
      case MESA_SHADER_CLOSEST_HIT:
      case MESA_SHADER_MISS:
      case MESA_SHADER_INTERSECTION:
      case MESA_SHADER_CALLABLE:
         populate_bs_prog_key(&shader_data->key.bs, vk_device->physical,
                              info->robustness, info->rt_flags);
         break;
      default:
         UNREACHABLE("Invalid stage");
      }
   }

   if (INTEL_DEBUG(DEBUG_MDA))
      anv_debug_archiver_init(mem_ctx, shaders_data, shader_count);

   {
      /* We're going to do cross stage link if we have a fragment shader with
       * any other stage (that would include all the associated
       * pre-rasterization stages of the pipeline).
       */
      const bool separate_shaders =
         !(shader_count > 1 && ordered_infos[MESA_SHADER_FRAGMENT] != NULL);

      for (uint32_t s = 0; s < shader_count; s++)
         shaders_data[s].info->nir->info.separate_shader = separate_shaders;
   }

   if (mesa_shader_stage_is_graphics(shaders_data[0].info->stage)) {
      anv_shaders_pre_lower_gfx(device, shaders_data, shader_count,
                                state, mem_ctx);
   }

   for (uint32_t s = 0; s < shader_count; s++) {
      struct anv_shader_data *shader_data = &shaders_data[s];

      anv_shader_lower_nir(device, mem_ctx, state, shader_data);

      anv_fixup_subgroup_size(device, shader_data->info->nir);

      anv_nir_apply_shader_workarounds(shader_data->info->nir);
   }

   /* Combine intersection & any-hit before lowering */
   if (ordered_infos[MESA_SHADER_INTERSECTION] != NULL) {
      brw_nir_lower_combined_intersection_any_hit(
         ordered_infos[MESA_SHADER_INTERSECTION]->nir,
         ordered_infos[MESA_SHADER_ANY_HIT] != NULL ?
         ordered_infos[MESA_SHADER_ANY_HIT]->nir : NULL,
         device->info);
   }

   if (mesa_shader_stage_is_graphics(shaders_data[0].info->stage))
      anv_shaders_post_lower_gfx(device, shaders_data, shader_count, state);
   else if (mesa_shader_stage_is_rt(shaders_data[0].info->stage))
      anv_shaders_post_lower_rt(device, shaders_data, shader_count);

   for (uint32_t s = 0; s < shader_count; s++) {
      struct anv_shader_data *shader_data = &shaders_data[s];
      struct anv_shader_data *prev_shader_data =
         s > 0 ? &shaders_data[s - 1] : NULL;

      char *error_str = NULL;
      switch (shader_data->info->stage) {
      case MESA_SHADER_VERTEX:
         anv_shader_compile_vs(device, mem_ctx, shader_data, &error_str);
         break;
      case MESA_SHADER_TESS_CTRL:
         anv_shader_compile_tcs(device, mem_ctx, shader_data, &error_str);
         break;
      case MESA_SHADER_TESS_EVAL:
         anv_shader_compile_tes(device, mem_ctx,
                                &shaders_data[s], prev_shader_data,
                                &error_str);
         break;
      case MESA_SHADER_GEOMETRY:
         anv_shader_compile_gs(device, mem_ctx, shader_data, &error_str);
         break;
      case MESA_SHADER_TASK:
         anv_shader_compile_task(device, mem_ctx, shader_data, &error_str);
         break;
      case MESA_SHADER_MESH:
         anv_shader_compile_mesh(device, mem_ctx,
                                 &shaders_data[s], prev_shader_data,
                                 &error_str);
         break;
      case MESA_SHADER_FRAGMENT:
         anv_shader_compile_fs(device, mem_ctx, shader_data, state, &error_str);
         break;
      case MESA_SHADER_COMPUTE:
         anv_shader_compile_cs(device, mem_ctx, shader_data, &error_str);
         break;
      case MESA_SHADER_RAYGEN:
      case MESA_SHADER_ANY_HIT:
      case MESA_SHADER_CLOSEST_HIT:
      case MESA_SHADER_MISS:
      case MESA_SHADER_INTERSECTION:
      case MESA_SHADER_CALLABLE:
         anv_shader_compile_bs(device, mem_ctx, shader_data, &error_str);
         break;
      default:
         UNREACHABLE("Invalid graphics shader stage");
      }

      if (shader_data->code == NULL) {
         if (error_str)
            result = vk_errorf(device, VK_ERROR_UNKNOWN, "%s", error_str);
         else
            result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
         goto end;
      }

      anv_nir_validate_push_layout(device->physical,
                                   &shader_data->prog_data.base,
                                   &shader_data->bind_map);

      shader_data->xfb_info = shader_data->info->nir->xfb_info;

      result = anv_shader_create(device, shader_data->info->stage,
                                 mem_ctx, shader_data, pAllocator,
                                 shader_data->shader_out);
      if (result != VK_SUCCESS)
         goto end;
   }

end:
   if (INTEL_DEBUG(DEBUG_MDA))
      anv_debug_archiver_finish(shaders_data, shader_count);

   ralloc_free(mem_ctx);

#if 0
   /* TODO: Write the feedback index into the pipeline */
   for (unsigned s = 0; s < ARRAY_SIZE(pipeline->shaders); s++) {
      anv_pipeline_account_shader(&pipeline->base, pipeline->shaders[s]);
   }
#endif

   if (result != VK_SUCCESS) {
      for (unsigned s = 0; s < shader_count; s++) {
         if (shaders_out[s] != NULL)
            vk_shader_free(vk_device, &vk_device->alloc, shaders_out[s]);
      }
   }

   return result;
}

static void
anv_write_rt_shader_group(struct vk_device *vk_device,
                          VkRayTracingShaderGroupTypeKHR type,
                          const struct vk_shader **shaders,
                          uint32_t shader_count,
                          void *output)
{
   struct anv_device *device =
      container_of(vk_device, struct anv_device, vk);

   anv_genX(device->info, write_rt_shader_group)(device, type,
                                                 shaders, shader_count,
                                                 output);
}

static void
anv_write_rt_shader_group_replay_handle(struct vk_device *device,
                                        const struct vk_shader **shaders,
                                        uint32_t shader_count,
                                        void *output)
{
   UNREACHABLE("Unimplemented");
}

struct vk_device_shader_ops anv_device_shader_ops = {
   .get_nir_options                = anv_shader_get_nir_options,
   .get_spirv_options              = anv_shader_get_spirv_options,
   .preprocess_nir                 = anv_shader_preprocess_nir,
   .get_rt_group_linking           = anv_shader_get_rt_group_linking,
   .hash_state                     = anv_shader_hash_state,
   .compile                        = anv_shader_compile,
   .deserialize                    = anv_shader_deserialize,
   .write_rt_shader_group          = anv_write_rt_shader_group,
   .write_rt_shader_group_replay_handle = anv_write_rt_shader_group_replay_handle,
   .cmd_bind_shaders               = anv_cmd_buffer_bind_shaders,
   .cmd_set_dynamic_graphics_state = vk_cmd_set_dynamic_graphics_state,
   .cmd_set_rt_state               = anv_cmd_buffer_set_rt_state,
   .cmd_set_stack_size             = anv_cmd_buffer_set_stack_size,
};
