/*
 * Copyright © 2021 Google
 *
 * SPDX-License-Identifier: MIT
 */

#include "nir/nir.h"
#include "nir/nir_builder.h"
#include "nir/nir_serialize.h"

#include "ac_shader_util.h"
#include "vk_shader_module.h"

#include "nir/radv_nir.h"
#include "nir/radv_nir_rt_stage_cps.h"
#include "nir/radv_nir_rt_stage_functions.h"
#include "nir/radv_nir_rt_stage_monolithic.h"
#include "nir/radv_nir_rt_traversal_shader.h"
#include "ac_nir.h"
#include "radv_debug.h"
#include "radv_descriptor_set.h"
#include "radv_entrypoints.h"
#include "radv_pipeline_binary.h"
#include "radv_pipeline_cache.h"
#include "radv_pipeline_layout.h"
#include "radv_pipeline_rt.h"

#include "nir/radv_nir_rt_stage_common.h"
#include "aco_interface.h"
#include "aco_nir_call_attribs.h"
#include "radv_aco_shader_info.h"
#include "radv_rmv.h"
#include "radv_shader.h"

struct rt_handle_hash_entry {
   uint32_t key;
   char hash[BLAKE3_KEY_LEN];
};

static uint32_t
handle_from_stages(struct radv_device *device, const unsigned char *shader_blake3, bool replay_namespace)
{
   uint32_t ret;

   memcpy(&ret, shader_blake3, sizeof(ret));

   /* Leave the low half for resume shaders etc. */
   ret |= 1u << 31;

   /* Ensure we have dedicated space for replayable shaders */
   ret &= ~(1u << 30);
   ret |= replay_namespace << 30;

   simple_mtx_lock(&device->rt_handles_mtx);

   struct hash_entry *he = NULL;
   for (;;) {
      he = _mesa_hash_table_search(device->rt_handles, &ret);
      if (!he)
         break;

      if (memcmp(he->data, shader_blake3, BLAKE3_KEY_LEN) == 0)
         break;

      ++ret;
   }

   if (!he) {
      struct rt_handle_hash_entry *e = ralloc(device->rt_handles, struct rt_handle_hash_entry);
      e->key = ret;
      memcpy(e->hash, shader_blake3, BLAKE3_KEY_LEN);
      _mesa_hash_table_insert(device->rt_handles, &e->key, &e->hash);
   }

   simple_mtx_unlock(&device->rt_handles_mtx);

   return ret;
}

static void
radv_generate_rt_shaders_key(const struct radv_device *device, const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                             struct radv_shader_stage_key *stage_keys)
{
   VkPipelineCreateFlags2 create_flags = vk_rt_pipeline_create_flags(pCreateInfo);

   for (uint32_t i = 0; i < pCreateInfo->stageCount; i++) {
      const VkPipelineShaderStageCreateInfo *stage = &pCreateInfo->pStages[i];
      mesa_shader_stage s = vk_to_mesa_shader_stage(stage->stage);

      stage_keys[s] = radv_pipeline_get_shader_key(device, stage, create_flags, pCreateInfo->pNext);
   }

   if (pCreateInfo->pLibraryInfo) {
      for (unsigned i = 0; i < pCreateInfo->pLibraryInfo->libraryCount; ++i) {
         VK_FROM_HANDLE(radv_pipeline, pipeline_lib, pCreateInfo->pLibraryInfo->pLibraries[i]);
         struct radv_ray_tracing_pipeline *library_pipeline = radv_pipeline_to_ray_tracing(pipeline_lib);
         /* apply shader robustness from merged shaders */
         if (library_pipeline->traversal_storage_robustness2)
            stage_keys[MESA_SHADER_INTERSECTION].storage_robustness2 = true;

         if (library_pipeline->traversal_uniform_robustness2)
            stage_keys[MESA_SHADER_INTERSECTION].uniform_robustness2 = true;
      }
   }
}

static VkResult
radv_create_group_handles(struct radv_device *device, const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                          const struct radv_ray_tracing_stage *stages, struct radv_ray_tracing_group *groups)
{
   VkPipelineCreateFlags2 create_flags = vk_rt_pipeline_create_flags(pCreateInfo);
   bool capture_replay = create_flags & VK_PIPELINE_CREATE_2_RAY_TRACING_SHADER_GROUP_HANDLE_CAPTURE_REPLAY_BIT_KHR;
   for (unsigned i = 0; i < pCreateInfo->groupCount; ++i) {
      const VkRayTracingShaderGroupCreateInfoKHR *group_info = &pCreateInfo->pGroups[i];
      switch (group_info->type) {
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR:
         if (group_info->generalShader != VK_SHADER_UNUSED_KHR) {
            const struct radv_ray_tracing_stage *stage = &stages[group_info->generalShader];
            groups[i].handle.general_index = handle_from_stages(device, stage->blake3, capture_replay);
         }
         break;
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR:
         if (group_info->closestHitShader != VK_SHADER_UNUSED_KHR) {
            const struct radv_ray_tracing_stage *stage = &stages[group_info->closestHitShader];
            groups[i].handle.closest_hit_index = handle_from_stages(device, stage->blake3, capture_replay);
         }

         if (group_info->intersectionShader != VK_SHADER_UNUSED_KHR) {
            unsigned char blake3[BLAKE3_KEY_LEN];
            blake3_hasher ctx;

            _mesa_blake3_init(&ctx);
            _mesa_blake3_update(&ctx, stages[group_info->intersectionShader].blake3, BLAKE3_KEY_LEN);
            if (group_info->anyHitShader != VK_SHADER_UNUSED_KHR)
               _mesa_blake3_update(&ctx, stages[group_info->anyHitShader].blake3, BLAKE3_KEY_LEN);
            _mesa_blake3_final(&ctx, blake3);

            groups[i].handle.intersection_index = handle_from_stages(device, blake3, capture_replay);
         }
         break;
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR:
         if (group_info->closestHitShader != VK_SHADER_UNUSED_KHR) {
            const struct radv_ray_tracing_stage *stage = &stages[group_info->closestHitShader];
            groups[i].handle.closest_hit_index = handle_from_stages(device, stage->blake3, capture_replay);
         }

         if (group_info->anyHitShader != VK_SHADER_UNUSED_KHR) {
            const struct radv_ray_tracing_stage *stage = &stages[group_info->anyHitShader];
            groups[i].handle.any_hit_index = handle_from_stages(device, stage->blake3, capture_replay);
         }
         break;
      default:
         UNREACHABLE("Invalid RT shader group type");
      }

      if (group_info->pShaderGroupCaptureReplayHandle) {
         const struct radv_rt_capture_replay_handle *handle = group_info->pShaderGroupCaptureReplayHandle;
         if (memcmp(&handle->non_recursive_idx, &groups[i].handle.any_hit_index, sizeof(uint32_t)) != 0) {
            return VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS;
         }
      }
   }

   return VK_SUCCESS;
}

static VkResult
radv_rt_init_capture_replay(struct radv_device *device, const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                            const struct radv_ray_tracing_stage *stages, const struct radv_ray_tracing_group *groups,
                            struct radv_serialized_shader_arena_block *capture_replay_blocks)
{
   VkResult result = VK_SUCCESS;
   uint32_t idx;

   for (idx = 0; idx < pCreateInfo->groupCount; idx++) {
      if (!pCreateInfo->pGroups[idx].pShaderGroupCaptureReplayHandle)
         continue;

      const struct radv_rt_capture_replay_handle *handle =
         (const struct radv_rt_capture_replay_handle *)pCreateInfo->pGroups[idx].pShaderGroupCaptureReplayHandle;

      capture_replay_blocks[idx] = handle->ahit_isec_alloc;

      if (groups[idx].recursive_shader < pCreateInfo->stageCount) {
         capture_replay_blocks[pCreateInfo->groupCount + groups[idx].recursive_shader] = handle->recursive_shader_alloc;
      } else if (groups[idx].recursive_shader != VK_SHADER_UNUSED_KHR) {
         struct radv_shader *library_shader = stages[groups[idx].recursive_shader].shader;
         simple_mtx_lock(&library_shader->replay_mtx);
         /* If arena_va is 0, the pipeline is monolithic and the shader was inlined into raygen */
         if (!library_shader->has_replay_alloc && handle->recursive_shader_alloc.arena_va) {
            union radv_shader_arena_block *new_block =
               radv_replay_shader_arena_block(device, &handle->recursive_shader_alloc, library_shader);
            if (!new_block) {
               result = VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS;
               goto reloc_out;
            }

            radv_shader_wait_for_upload(device, library_shader->upload_seq);
            radv_free_shader_memory(device, library_shader->alloc);

            library_shader->alloc = new_block;
            library_shader->has_replay_alloc = true;

            library_shader->bo = library_shader->alloc->arena->bo;
            library_shader->va = radv_buffer_get_va(library_shader->bo) + library_shader->alloc->offset;

            if (!radv_shader_reupload(device, library_shader)) {
               result = VK_ERROR_UNKNOWN;
               goto reloc_out;
            }
         }

      reloc_out:
         simple_mtx_unlock(&library_shader->replay_mtx);
         if (result != VK_SUCCESS)
            return result;
      }
   }

   return result;
}

static VkResult
radv_rt_fill_group_info(struct radv_device *device, const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                        const struct radv_ray_tracing_stage *stages, struct radv_ray_tracing_group *groups)
{
   VkResult result = radv_create_group_handles(device, pCreateInfo, stages, groups);

   uint32_t idx;
   for (idx = 0; idx < pCreateInfo->groupCount; idx++) {
      groups[idx].type = pCreateInfo->pGroups[idx].type;
      if (groups[idx].type == VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR)
         groups[idx].recursive_shader = pCreateInfo->pGroups[idx].generalShader;
      else
         groups[idx].recursive_shader = pCreateInfo->pGroups[idx].closestHitShader;
      groups[idx].any_hit_shader = pCreateInfo->pGroups[idx].anyHitShader;
      groups[idx].intersection_shader = pCreateInfo->pGroups[idx].intersectionShader;
   }

   /* copy and adjust library groups (incl. handles) */
   if (pCreateInfo->pLibraryInfo) {
      unsigned stage_count = pCreateInfo->stageCount;
      for (unsigned i = 0; i < pCreateInfo->pLibraryInfo->libraryCount; ++i) {
         VK_FROM_HANDLE(radv_pipeline, pipeline_lib, pCreateInfo->pLibraryInfo->pLibraries[i]);
         struct radv_ray_tracing_pipeline *library_pipeline = radv_pipeline_to_ray_tracing(pipeline_lib);

         for (unsigned j = 0; j < library_pipeline->group_count; ++j) {
            struct radv_ray_tracing_group *dst = &groups[idx + j];
            *dst = library_pipeline->groups[j];
            if (library_pipeline->groups[j].ahit_isec_shader)
               dst->ahit_isec_shader = radv_shader_ref(library_pipeline->groups[j].ahit_isec_shader);
            if (dst->recursive_shader != VK_SHADER_UNUSED_KHR)
               dst->recursive_shader += stage_count;
            if (dst->any_hit_shader != VK_SHADER_UNUSED_KHR)
               dst->any_hit_shader += stage_count;
            if (dst->intersection_shader != VK_SHADER_UNUSED_KHR)
               dst->intersection_shader += stage_count;
            /* Don't set the shader VA since the handles are part of the pipeline hash */
            dst->handle.recursive_shader_ptr = 0;
            dst->handle.ahit_isec_ptr = 0;
         }
         idx += library_pipeline->group_count;
         stage_count += library_pipeline->stage_count;
      }
   }

   return result;
}

static void
radv_rt_fill_stage_info(const VkRayTracingPipelineCreateInfoKHR *pCreateInfo, struct radv_ray_tracing_stage *stages)
{
   uint32_t idx;
   for (idx = 0; idx < pCreateInfo->stageCount; idx++) {
      stages[idx].stage = vk_to_mesa_shader_stage(pCreateInfo->pStages[idx].stage);
      stages[idx].needs_nir = true;
   }

   if (pCreateInfo->pLibraryInfo) {
      for (unsigned i = 0; i < pCreateInfo->pLibraryInfo->libraryCount; ++i) {
         VK_FROM_HANDLE(radv_pipeline, pipeline, pCreateInfo->pLibraryInfo->pLibraries[i]);
         struct radv_ray_tracing_pipeline *library_pipeline = radv_pipeline_to_ray_tracing(pipeline);
         for (unsigned j = 0; j < library_pipeline->stage_count; ++j) {
            if (library_pipeline->stages[j].nir)
               stages[idx].nir = vk_pipeline_cache_object_ref(library_pipeline->stages[j].nir);
            if (library_pipeline->stages[j].shader)
               stages[idx].shader = radv_shader_ref(library_pipeline->stages[j].shader);

            stages[idx].stage = library_pipeline->stages[j].stage;
            stages[idx].stack_size = library_pipeline->stages[j].stack_size;
            stages[idx].info = library_pipeline->stages[j].info;
            memcpy(stages[idx].blake3, library_pipeline->stages[j].blake3, BLAKE3_KEY_LEN);
            idx++;
         }
      }
   }
}

static void
radv_init_rt_stage_hashes(const struct radv_device *device, VkPipelineCreateFlags2 pipeline_flags,
                          const VkRayTracingPipelineCreateInfoKHR *pCreateInfo, struct radv_ray_tracing_stage *stages,
                          const struct radv_shader_stage_key *stage_keys)
{
   const VkPipelineBinaryInfoKHR *binary_info = vk_find_struct_const(pCreateInfo->pNext, PIPELINE_BINARY_INFO_KHR);
   if (binary_info && binary_info->binaryCount > 0) {
      for (uint32_t i = 0; i < binary_info->binaryCount; i++) {
         VK_FROM_HANDLE(radv_pipeline_binary, pipeline_binary, binary_info->pPipelineBinaries[i]);
         struct blob_reader blob;

         blob_reader_init(&blob, pipeline_binary->data, pipeline_binary->size);

         const struct radv_ray_tracing_binary_header *header =
            (const struct radv_ray_tracing_binary_header *)blob_read_bytes(&blob, sizeof(*header));

         if (header->is_traversal_shader)
            continue;

         memcpy(stages[i].blake3, header->stage_blake3, BLAKE3_KEY_LEN);
      }
   } else {
      for (uint32_t idx = 0; idx < pCreateInfo->stageCount; idx++) {
         const VkPipelineShaderStageCreateInfo *sinfo = &pCreateInfo->pStages[idx];
         mesa_shader_stage s = vk_to_mesa_shader_stage(sinfo->stage);
         blake3_hasher ctx;

         _mesa_blake3_init(&ctx);
         radv_pipeline_hash_shader_stage(pipeline_flags, sinfo, &stage_keys[s], &ctx);
         _mesa_blake3_final(&ctx, stages[idx].blake3);
      }
   }
}

static bool
should_move_rt_instruction(nir_intrinsic_instr *instr)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_load_hit_attrib_amd:
      return nir_intrinsic_base(instr) < RADV_MAX_HIT_ATTRIB_DWORDS;
   case nir_intrinsic_load_ray_flags:
   case nir_intrinsic_load_ray_object_origin:
   case nir_intrinsic_load_ray_world_origin:
   case nir_intrinsic_load_ray_t_min:
   case nir_intrinsic_load_ray_object_direction:
   case nir_intrinsic_load_ray_world_direction:
   case nir_intrinsic_load_ray_t_max:
      return true;
   default:
      return false;
   }
}

static bool
move_rt_instructions(nir_shader *shader)
{
   bool progress = false;
   nir_cursor target = nir_before_impl(nir_shader_get_entrypoint(shader));

   nir_foreach_block (block, nir_shader_get_entrypoint(shader)) {
      nir_foreach_instr_safe (instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrinsic = nir_instr_as_intrinsic(instr);

         if (!should_move_rt_instruction(intrinsic))
            continue;

         progress = true;
         nir_instr_move(target, instr);
      }
   }

   return nir_progress(progress, nir_shader_get_entrypoint(shader), nir_metadata_control_flow);
}

static void
radv_rt_nir_to_asm(struct radv_device *device, struct radv_ray_tracing_pipeline *pipeline,
                   enum radv_rt_lowering_mode mode, struct radv_shader_stage *stage, uint32_t *payload_size,
                   uint32_t *hit_attrib_size, struct radv_ray_tracing_stage_info *stage_info,
                   const struct radv_ray_tracing_stage_info *traversal_stage_info, bool has_position_fetch,
                   struct radv_shader_binary **binary, struct radv_shader_debug_info *debug)
{
   struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_instance *instance = radv_physical_device_instance(pdev);

   bool keep_executable_info = radv_pipeline_capture_shaders(device, pipeline->base.base.create_flags);
   bool keep_statistic_info = radv_pipeline_capture_shader_stats(device, pipeline->base.base.create_flags);

   switch (mode) {
   case RADV_RT_LOWERING_MODE_MONOLITHIC:
      radv_nir_lower_rt_io_monolithic(stage->nir);
      break;
   case RADV_RT_LOWERING_MODE_CPS:
      radv_nir_lower_rt_io_cps(stage->nir);
      break;
   case RADV_RT_LOWERING_MODE_FUNCTION_CALLS:
      radv_nir_lower_rt_io_functions(stage->nir);
      break;
   }

   /* Gather shader info. */
   nir_shader_gather_info(stage->nir, nir_shader_get_entrypoint(stage->nir));
   radv_nir_shader_info_init(stage->stage, MESA_SHADER_NONE, &stage->info);
   radv_nir_shader_info_pass(device, stage->nir, &stage->layout, &stage->key, NULL, RADV_PIPELINE_RAY_TRACING, false,
                             &stage->info);

   /* Declare shader arguments. */
   radv_declare_shader_args(device, NULL, &stage->info, stage->stage, MESA_SHADER_NONE, &stage->args, debug);

   stage->info.user_sgprs_locs = stage->args.user_sgprs_locs;
   stage->info.inline_push_constant_mask = stage->args.ac.inline_push_const_mask;
   stage->info.type = radv_is_traversal_shader(stage->nir) ? RADV_SHADER_TYPE_RT_TRAVERSAL : RADV_SHADER_TYPE_DEFAULT;

   uint32_t num_resume_shaders = 0;
   nir_shader **resume_shaders = NULL;
   void *mem_ctx = ralloc_context(NULL);

   if (stage->stage != MESA_SHADER_INTERSECTION && mode == RADV_RT_LOWERING_MODE_CPS) {
      /* Move ray tracing system values to the top that are set by rt_trace_ray
       * to prevent them from being overwritten by other rt_trace_ray calls.
       */
      NIR_PASS(_, stage->nir, move_rt_instructions);

      nir_builder b = nir_builder_at(nir_after_impl(nir_shader_get_entrypoint(stage->nir)));
      nir_rt_return_amd(&b);

      const nir_lower_shader_calls_options opts = {
         .address_format = nir_address_format_32bit_offset,
         .stack_alignment = 16,
         .localized_loads = true,
         .vectorizer_callback = ac_nir_mem_vectorize_callback,
         .vectorizer_data = &(struct ac_nir_config){pdev->info.gfx_level, !pdev->use_llvm},
      };
      nir_lower_shader_calls(stage->nir, &opts, &resume_shaders, &num_resume_shaders, mem_ctx);
   }

   unsigned num_shaders = num_resume_shaders + 1;
   nir_shader **shaders = ralloc_array(mem_ctx, nir_shader *, num_shaders);

   shaders[0] = stage->nir;
   for (uint32_t i = 0; i < num_resume_shaders; i++)
      shaders[i + 1] = resume_shaders[i];

   if (stage_info)
      memset(stage_info->unused_args, 0xFF, sizeof(stage_info->unused_args));

   /* Postprocess shader parts. */
   for (uint32_t i = 0; i < num_shaders; i++) {
      struct radv_shader_stage temp_stage = *stage;
      temp_stage.nir = shaders[i];
      switch (mode) {
      case RADV_RT_LOWERING_MODE_MONOLITHIC:
         assert(num_shaders == 1);
         radv_nir_lower_rt_abi_monolithic(temp_stage.nir, device, pipeline);
         break;
      case RADV_RT_LOWERING_MODE_CPS:
         radv_nir_lower_rt_abi_cps(temp_stage.nir, &stage->info, i > 0, device, pipeline, has_position_fetch,
                                   traversal_stage_info);
         break;
      case RADV_RT_LOWERING_MODE_FUNCTION_CALLS:
         assert(num_shaders == 1);
         radv_nir_lower_rt_abi_functions(temp_stage.nir, &temp_stage.info, *payload_size, *hit_attrib_size, device,
                                         pipeline);
         break;
      }

      /* Info might be out-of-date after inlining in radv_nir_lower_rt_abi(). */
      nir_shader_gather_info(temp_stage.nir, radv_get_rt_shader_entrypoint(temp_stage.nir));

      radv_nir_shader_info_pass(device, temp_stage.nir, &stage->layout, &stage->key, NULL, RADV_PIPELINE_RAY_TRACING,
                                false, &stage->info);

      radv_optimize_nir(temp_stage.nir, temp_stage.key.optimisations_disabled);
      radv_postprocess_nir(device, NULL, &temp_stage);

      NIR_PASS(_, stage->nir, radv_nir_lower_call_abi, stage->info.wave_size);
      NIR_PASS(_, stage->nir, nir_lower_global_vars_to_local);
      NIR_PASS(_, stage->nir, nir_lower_vars_to_ssa);
      NIR_PASS(_, stage->nir, nir_opt_copy_prop);
      NIR_PASS(_, stage->nir, nir_opt_remove_phis);
      if (!stage->key.optimisations_disabled && !radv_is_traversal_shader(stage->nir))
         NIR_PASS(_, stage->nir, nir_minimize_call_live_states);

      stage->info.nir_shared_size = MAX2(stage->info.nir_shared_size, temp_stage.info.nir_shared_size);
      if (stage_info && mode == RADV_RT_LOWERING_MODE_CPS)
         radv_gather_unused_args(stage_info, temp_stage.nir);
   }

   debug->dump_shader = radv_can_dump_shader(device, stage->nir);
   bool dump_nir = debug->dump_shader && (instance->debug_flags & RADV_DEBUG_DUMP_NIR);

   if (debug->dump_shader) {
      simple_mtx_lock(&instance->shader_dump_mtx);

      if (dump_nir) {
         for (uint32_t i = 0; i < num_shaders; i++)
            nir_print_shader(shaders[i], stderr);
      }
   }

   /* Compile NIR shader to AMD assembly. */
   *binary =
      radv_shader_nir_to_asm(device, stage, shaders, num_shaders, NULL, keep_executable_info, keep_statistic_info);

   /* Dump NIR after nir_to_asm, because ACO modifies it. */
   if (keep_executable_info || debug->dump_shader)
      debug->nir_string = radv_dump_nir_shaders(instance, shaders, num_shaders);

   radv_parse_binary_debug_info(device, *binary, debug);
   debug->stages = 1 << shaders[0]->info.stage;

   radv_shader_dump_asm(device, debug, &stage->info);

   if (keep_executable_info && stage->spirv.size) {
      debug->spirv = malloc(stage->spirv.size);
      memcpy(debug->spirv, stage->spirv.data, stage->spirv.size);
      debug->spirv_size = stage->spirv.size;
   }

   if (debug->dump_shader)
      simple_mtx_unlock(&instance->shader_dump_mtx);

   ralloc_free(mem_ctx);
}

static VkResult
radv_rt_compile_nir(struct radv_device *device, struct vk_pipeline_cache *cache,
                    struct radv_ray_tracing_pipeline *pipeline, enum radv_rt_lowering_mode mode,
                    struct radv_shader_stage *stage, uint32_t *payload_size, uint32_t *hit_attrib_size,
                    uint32_t *stack_size, struct radv_ray_tracing_stage_info *stage_info,
                    const struct radv_ray_tracing_stage_info *traversal_stage_info,
                    struct radv_serialized_shader_arena_block *replay_block, bool skip_shaders_cache,
                    bool has_position_fetch, struct radv_shader **out_shader)
{
   bool replayable = (pipeline->base.base.create_flags &
                      VK_PIPELINE_CREATE_2_RAY_TRACING_SHADER_GROUP_HANDLE_CAPTURE_REPLAY_BIT_KHR) &&
                     !radv_is_traversal_shader(stage->nir);

   struct radv_shader_binary *binary;
   struct radv_shader_debug_info debug = {0};
   radv_rt_nir_to_asm(device, pipeline, mode, stage, payload_size, hit_attrib_size, stage_info, traversal_stage_info,
                      has_position_fetch, &binary, &debug);

   struct radv_shader *shader;
   if (replay_block || replayable) {
      VkResult result = radv_shader_create_uncached(device, binary, replayable, replay_block, &debug, &shader);
      if (result != VK_SUCCESS) {
         free(binary);
         return result;
      }
   } else {
      shader = radv_shader_create(device, cache, binary, skip_shaders_cache, &debug);
   }

   if (shader && stack_size)
      *stack_size = DIV_ROUND_UP(shader->config.scratch_bytes_per_wave, shader->info.wave_size);

   free(binary);

   *out_shader = shader;

   if (radv_can_dump_shader_stats(device, stage->nir))
      radv_dump_shader_stats(device, &pipeline->base.base, shader, stderr);

   return shader ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
}

static void
radv_update_const_info(enum radv_rt_const_arg_state *state, bool equal)
{
   if (*state == RADV_RT_CONST_ARG_STATE_UNINITIALIZED)
      *state = RADV_RT_CONST_ARG_STATE_VALID;
   else if (*state == RADV_RT_CONST_ARG_STATE_VALID && !equal)
      *state = RADV_RT_CONST_ARG_STATE_INVALID;
}

static void
radv_gather_trace_ray_src(struct radv_rt_const_arg_info *info, nir_src src)
{
   if (nir_src_is_const(src)) {
      radv_update_const_info(&info->state, info->value == nir_src_as_uint(src));
      info->value = nir_src_as_uint(src);
   } else {
      info->state = RADV_RT_CONST_ARG_STATE_INVALID;
   }
}

static void
radv_rt_const_arg_info_combine(struct radv_rt_const_arg_info *dst, const struct radv_rt_const_arg_info *src)
{
   if (src->state != RADV_RT_CONST_ARG_STATE_UNINITIALIZED) {
      radv_update_const_info(&dst->state, dst->value == src->value);
      if (src->state == RADV_RT_CONST_ARG_STATE_INVALID)
         dst->state = RADV_RT_CONST_ARG_STATE_INVALID;
      dst->value = src->value;
   }
}

static struct radv_ray_tracing_stage_info
radv_gather_ray_tracing_stage_info(nir_shader *nir)
{
   struct radv_ray_tracing_stage_info info = {
      .can_inline = true,
      .set_flags = 0xFFFFFFFF,
      .unset_flags = 0xFFFFFFFF,
   };

   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   nir_foreach_block (block, impl) {
      nir_foreach_instr (instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
         if (intr->intrinsic == nir_intrinsic_load_ray_triangle_vertex_positions)
            info.has_position_fetch = true;

         if (intr->intrinsic != nir_intrinsic_trace_ray)
            continue;

         info.can_inline = false;

         radv_gather_trace_ray_src(&info.tmin, intr->src[7]);
         radv_gather_trace_ray_src(&info.tmax, intr->src[9]);
         radv_gather_trace_ray_src(&info.sbt_offset, intr->src[3]);
         radv_gather_trace_ray_src(&info.sbt_stride, intr->src[4]);
         radv_gather_trace_ray_src(&info.miss_index, intr->src[5]);

         nir_src flags = intr->src[1];
         if (nir_src_is_const(flags)) {
            info.set_flags &= nir_src_as_uint(flags);
            info.unset_flags &= ~nir_src_as_uint(flags);
         } else {
            info.set_flags = 0;
            info.unset_flags = 0;
         }
      }
   }

   if (nir->info.stage == MESA_SHADER_RAYGEN || nir->info.stage == MESA_SHADER_ANY_HIT ||
       nir->info.stage == MESA_SHADER_INTERSECTION)
      info.can_inline = true;
   else if (nir->info.stage == MESA_SHADER_CALLABLE)
      info.can_inline = false;

   return info;
}

static inline bool
radv_ray_tracing_stage_nir_always_needed(struct radv_ray_tracing_stage *stage)
{
   return stage->stage == MESA_SHADER_ANY_HIT || stage->stage == MESA_SHADER_INTERSECTION;
}

static void
radv_rt_spirv_to_nir(struct radv_device *device, struct radv_shader_stage *stage, uint32_t *payload_size,
                     uint32_t *hit_attrib_size, struct radv_ray_tracing_stage_info *info)
{
   stage->nir = radv_shader_spirv_to_nir(device, stage, NULL, false);

   nir_foreach_variable_with_modes (var, stage->nir, nir_var_ray_hit_attrib) {
      unsigned size, alignment;
      glsl_get_natural_size_align_bytes(var->type, &size, &alignment);
      *hit_attrib_size = MAX2(*hit_attrib_size, var->data.driver_location + size);
   }

   NIR_PASS(_, stage->nir, radv_nir_lower_hit_attrib_derefs);

   nir_foreach_variable_with_modes (var, stage->nir, nir_var_shader_call_data) {
      unsigned size, alignment;
      glsl_get_natural_size_align_bytes(var->type, &size, &alignment);
      *payload_size = MAX2(*payload_size, size);
   }
   nir_foreach_function_impl (impl, stage->nir) {
      nir_foreach_variable_in_list (var, &impl->locals) {
         unsigned size, alignment;
         glsl_get_natural_size_align_bytes(var->type, &size, &alignment);
         *payload_size = MAX2(*payload_size, size);
      }
   }

   *info = radv_gather_ray_tracing_stage_info(stage->nir);
}

static VkResult
radv_rt_compile_shaders(struct radv_device *device, struct vk_pipeline_cache *cache,
                        const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                        const VkPipelineCreationFeedbackCreateInfo *creation_feedback,
                        const struct radv_shader_stage_key *stage_keys, struct radv_ray_tracing_pipeline *pipeline,
                        struct radv_serialized_shader_arena_block *capture_replay_handles, bool skip_shaders_cache)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   VK_FROM_HANDLE(radv_pipeline_layout, pipeline_layout, pCreateInfo->layout);

   if (pipeline->base.base.create_flags & VK_PIPELINE_CREATE_2_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT)
      return VK_PIPELINE_COMPILE_REQUIRED;
   VkResult result = VK_SUCCESS;

   struct radv_ray_tracing_stage *rt_stages = pipeline->stages;

   struct radv_shader_stage *stages = calloc(pCreateInfo->stageCount, sizeof(struct radv_shader_stage));
   if (!stages)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   uint32_t payload_size = 0;
   uint32_t hit_attrib_size = 8;
   if (pCreateInfo->pLibraryInterface) {
      payload_size = pCreateInfo->pLibraryInterface->maxPipelineRayPayloadSize;
      hit_attrib_size = MAX2(hit_attrib_size, pCreateInfo->pLibraryInterface->maxPipelineRayHitAttributeSize);
   }

   bool library = pipeline->base.base.create_flags & VK_PIPELINE_CREATE_2_LIBRARY_BIT_KHR;

   /* Beyond 50 shader stages, inlining everything bloats the shader a ton, increasing compile times and
    * potentially even reducing runtime performance because of instruction cache coherency issues in the
    * traversal loop.
    */
   bool can_use_monolithic = !library && pipeline->stage_count < 50;

   for (uint32_t i = 0; i < pCreateInfo->stageCount; i++) {
      if (rt_stages[i].nir || !rt_stages[i].needs_nir)
         continue;

      int64_t stage_start = os_time_get_nano();

      struct radv_shader_stage *stage = &stages[i];
      mesa_shader_stage s = vk_to_mesa_shader_stage(pCreateInfo->pStages[i].stage);
      radv_pipeline_stage_init(pipeline->base.base.create_flags, &pCreateInfo->pStages[i], pipeline_layout,
                               &stage_keys[s], stage);

      /* precompile the shader */
      radv_rt_spirv_to_nir(device, stage, &payload_size, &hit_attrib_size, &rt_stages[i].info);

      stage->feedback.duration = os_time_get_nano() - stage_start;
   }

   bool has_callable = false;
   /* Libraries cannot know how they are used so we need to asssume that position fetch is used. */
   bool has_position_fetch = library;
   /* TODO: Recompile recursive raygen shaders instead. */
   bool raygen_imported = false;
   for (uint32_t i = 0; i < pipeline->stage_count; i++) {
      has_callable |= rt_stages[i].stage == MESA_SHADER_CALLABLE;
      has_position_fetch |= rt_stages[i].info.has_position_fetch;
      can_use_monolithic &= rt_stages[i].info.can_inline;

      if (i >= pCreateInfo->stageCount)
         raygen_imported |= rt_stages[i].stage == MESA_SHADER_RAYGEN;
   }

   enum radv_rt_lowering_mode recursive_lowering_mode =
      pdev->cache_key.rt_cps ? RADV_RT_LOWERING_MODE_CPS : RADV_RT_LOWERING_MODE_FUNCTION_CALLS;

   enum radv_rt_lowering_mode raygen_lowering_mode;
   if (can_use_monolithic)
      raygen_lowering_mode = RADV_RT_LOWERING_MODE_MONOLITHIC;
   else
      raygen_lowering_mode = recursive_lowering_mode;

   bool inline_any_hit_shaders = !library;
   uint32_t ahit_isec_stage_count = 0;
   for (uint32_t i = 0; i < pipeline->group_count; ++i) {
      if (pipeline->groups[i].any_hit_shader != VK_SHADER_UNUSED_KHR)
         ++ahit_isec_stage_count;
      if (pipeline->groups[i].intersection_shader != VK_SHADER_UNUSED_KHR)
         ++ahit_isec_stage_count;
   }
   inline_any_hit_shaders &= ahit_isec_stage_count < 20;
   /* Monolithic pipelines always inline any-hit/isec shaders. */
   inline_any_hit_shaders |= raygen_lowering_mode == RADV_RT_LOWERING_MODE_MONOLITHIC && !raygen_imported;

   for (uint32_t idx = 0; idx < pCreateInfo->stageCount; idx++) {
      if (rt_stages[idx].nir || !rt_stages[idx].needs_nir)
         continue;

      int64_t stage_start = os_time_get_nano();

      struct radv_shader_stage *stage = &stages[idx];

      /* Cases in which we need to keep around the NIR:
       *    - pipeline library: The final pipeline might be monolithic in which case it will need every NIR shader.
       *                        If there is a callable shader, we can be sure that the final pipeline won't be
       *                        monolithic.
       *    - non-recursive:    Non-recursive (Any-Hit/Intersection) shaders may be inlined into the traversal shader.
       *    - monolithic:       Callable shaders (chit/miss) are inlined into the raygen shader.
       */
      bool monolithic = raygen_lowering_mode == RADV_RT_LOWERING_MODE_MONOLITHIC;
      bool nir_always_needed = radv_ray_tracing_stage_nir_always_needed(&rt_stages[idx]);
      bool nir_needed =
         (library && !has_callable) || nir_always_needed || (monolithic && rt_stages[idx].stage != MESA_SHADER_RAYGEN);
      nir_needed &= !rt_stages[idx].nir;
      if (nir_needed) {
         const bool cached = !stage->key.optimisations_disabled &&
                             !(pipeline->base.base.create_flags & VK_PIPELINE_CREATE_2_CAPTURE_DATA_BIT_KHR);
         rt_stages[idx].stack_size = stage->nir->scratch_size;
         rt_stages[idx].nir = radv_pipeline_cache_nir_to_handle(device, cache, stage->nir, rt_stages[idx].blake3, cached);
      }

      stage->feedback.duration += os_time_get_nano() - stage_start;
   }

   for (uint32_t idx = 0; idx < pCreateInfo->stageCount; idx++) {
      int64_t stage_start = os_time_get_nano();
      struct radv_shader_stage *stage = &stages[idx];

      /* Cases in which we need to compile the shader (raygen/callable/chit/miss):
       *    TODO: - monolithic: Extend the loop to cover imported stages and force compilation of imported raygen
       *                        shaders since pipeline library shaders use separate compilation.
       *    - separate:   Compile any recursive stage if wasn't compiled yet. Non-recursive shaders are either inlined
       *                  into the traversal shader, or compiled separately per shader group.
       */
      bool shader_needed = !radv_ray_tracing_stage_nir_always_needed(&rt_stages[idx]) && !rt_stages[idx].shader;
      if (rt_stages[idx].stage != MESA_SHADER_RAYGEN)
         shader_needed &= raygen_lowering_mode != RADV_RT_LOWERING_MODE_MONOLITHIC || raygen_imported;

      if (shader_needed) {
         uint32_t stack_size = 0;
         struct radv_serialized_shader_arena_block *replay_block =
            capture_replay_handles[pCreateInfo->groupCount + idx].arena_va
               ? &capture_replay_handles[pCreateInfo->groupCount + idx]
               : NULL;

         enum radv_rt_lowering_mode mode =
            stage->stage == MESA_SHADER_RAYGEN ? raygen_lowering_mode : recursive_lowering_mode;

         result = radv_rt_compile_nir(device, cache, pipeline, mode, stage, &payload_size, &hit_attrib_size,
                                      &stack_size, &rt_stages[idx].info, NULL, replay_block, skip_shaders_cache,
                                      has_position_fetch, &rt_stages[idx].shader);
         if (result != VK_SUCCESS)
            goto cleanup;

         assert(rt_stages[idx].stack_size <= stack_size);
         rt_stages[idx].stack_size = stack_size;
      }

      if (creation_feedback && creation_feedback->pipelineStageCreationFeedbackCount) {
         assert(idx < creation_feedback->pipelineStageCreationFeedbackCount);
         stage->feedback.duration += os_time_get_nano() - stage_start;
         creation_feedback->pPipelineStageCreationFeedbacks[idx] = stage->feedback;
      }
   }

   if (!inline_any_hit_shaders) {
      /* Compile a shader for each ahit/isec group. */
      for (uint32_t idx = 0; idx < pCreateInfo->groupCount; idx++) {
         if (pCreateInfo->pGroups[idx].type == VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR)
            continue;
         if (pipeline->groups[idx].ahit_isec_shader)
            continue;

         uint32_t ahit_idx = pipeline->groups[idx].any_hit_shader;
         uint32_t isec_idx = pipeline->groups[idx].intersection_shader;

         if (ahit_idx == VK_SHADER_UNUSED_KHR && isec_idx == VK_SHADER_UNUSED_KHR)
            continue;

         nir_shader *ahit = NULL;
         nir_shader *isec = NULL;
         if (ahit_idx != VK_SHADER_UNUSED_KHR)
            ahit = radv_pipeline_cache_handle_to_nir(device, pipeline->stages[ahit_idx].nir);
         if (isec_idx != VK_SHADER_UNUSED_KHR)
            isec = radv_pipeline_cache_handle_to_nir(device, pipeline->stages[isec_idx].nir);

         nir_shader *final_shader;
         if (isec) {
            radv_nir_lower_intersection_shader(isec, ahit);
            final_shader = isec;
         } else {
            final_shader = ahit;
         }

         struct radv_shader_stage combined_stage = {
            .stage = final_shader->info.stage,
            .nir = final_shader,
            .key = stage_keys[final_shader->info.stage],
         };
         radv_shader_layout_init(pipeline_layout, final_shader->info.stage, &combined_stage.layout);
         uint32_t stack_size = 0;
         struct radv_serialized_shader_arena_block *replay_block =
            capture_replay_handles[idx].arena_va ? &capture_replay_handles[idx] : NULL;

         result = radv_rt_compile_nir(device, cache, pipeline, RADV_RT_LOWERING_MODE_FUNCTION_CALLS, &combined_stage,
                                      &payload_size, &hit_attrib_size, &stack_size, NULL, NULL, replay_block,
                                      skip_shaders_cache, has_position_fetch, &pipeline->groups[idx].ahit_isec_shader);
         if (result != VK_SUCCESS)
            goto cleanup;

         if (ahit_idx != VK_SHADER_UNUSED_KHR)
            rt_stages[ahit_idx].stack_size = MAX2(rt_stages[ahit_idx].stack_size, stack_size);
         if (isec_idx != VK_SHADER_UNUSED_KHR)
            rt_stages[isec_idx].stack_size = MAX2(rt_stages[isec_idx].stack_size, stack_size);
      }
   }

   /* Monolithic raygen shaders do not need a traversal shader. Skip compiling one if there are only monolithic raygen
    * shaders.
    */
   bool traversal_needed = !library && (raygen_lowering_mode != RADV_RT_LOWERING_MODE_MONOLITHIC || raygen_imported);
   if (!traversal_needed) {
      result = VK_SUCCESS;
      goto cleanup;
   }

   struct radv_ray_tracing_stage_info traversal_info = {
      .set_flags = 0xFFFFFFFF,
      .unset_flags = 0xFFFFFFFF,
      .has_position_fetch = has_position_fetch,
   };

   memset(traversal_info.unused_args, 0xFF, sizeof(traversal_info.unused_args));

   for (uint32_t i = 0; i < pipeline->stage_count; i++) {
      if (!pipeline->stages[i].shader)
         continue;

      struct radv_ray_tracing_stage_info *info = &pipeline->stages[i].info;

      BITSET_AND(traversal_info.unused_args, traversal_info.unused_args, info->unused_args);

      radv_rt_const_arg_info_combine(&traversal_info.tmin, &info->tmin);
      radv_rt_const_arg_info_combine(&traversal_info.tmax, &info->tmax);
      radv_rt_const_arg_info_combine(&traversal_info.sbt_offset, &info->sbt_offset);
      radv_rt_const_arg_info_combine(&traversal_info.sbt_stride, &info->sbt_stride);
      radv_rt_const_arg_info_combine(&traversal_info.miss_index, &info->miss_index);

      traversal_info.set_flags &= info->set_flags;
      traversal_info.unset_flags &= info->unset_flags;
   }

   radv_nir_traversal_preprocess_cb preprocess =
      recursive_lowering_mode == RADV_RT_LOWERING_MODE_CPS ? radv_nir_lower_rt_io_cps : radv_nir_lower_rt_io_functions;

   if (!inline_any_hit_shaders)
      preprocess = NULL;

   /* create traversal shader */
   nir_shader *traversal_nir =
      radv_build_traversal_shader(device, pipeline, &traversal_info, preprocess, payload_size, hit_attrib_size);
   struct radv_shader_stage traversal_stage = {
      .stage = MESA_SHADER_INTERSECTION,
      .nir = traversal_nir,
      .key = stage_keys[MESA_SHADER_INTERSECTION],
   };
   radv_shader_layout_init(pipeline_layout, MESA_SHADER_INTERSECTION, &traversal_stage.layout);
   result = radv_rt_compile_nir(device, cache, pipeline, recursive_lowering_mode, &traversal_stage, &payload_size,
                                &hit_attrib_size, &pipeline->traversal_stack_size, NULL, &traversal_info, NULL,
                                skip_shaders_cache, has_position_fetch,
                                &pipeline->base.base.shaders[MESA_SHADER_INTERSECTION]);
   ralloc_free(traversal_nir);

cleanup:
   for (uint32_t i = 0; i < pCreateInfo->stageCount; i++)
      radv_pipeline_stage_finish(&stages[i]);
   free(stages);
   return result;
}

static bool
radv_rt_pipeline_has_dynamic_stack_size(const VkRayTracingPipelineCreateInfoKHR *pCreateInfo)
{
   if (!pCreateInfo->pDynamicState)
      return false;

   for (unsigned i = 0; i < pCreateInfo->pDynamicState->dynamicStateCount; ++i) {
      if (pCreateInfo->pDynamicState->pDynamicStates[i] == VK_DYNAMIC_STATE_RAY_TRACING_PIPELINE_STACK_SIZE_KHR)
         return true;
   }

   return false;
}

static void
compute_rt_stack_size(const VkRayTracingPipelineCreateInfoKHR *pCreateInfo, struct radv_ray_tracing_pipeline *pipeline)
{
   if (radv_rt_pipeline_has_dynamic_stack_size(pCreateInfo)) {
      pipeline->stack_size = -1u;
      return;
   }

   unsigned raygen_size = 0;
   unsigned callable_size = 0;
   unsigned chit_miss_size = 0;
   unsigned intersection_size = 0;
   unsigned any_hit_size = 0;

   for (unsigned i = 0; i < pipeline->stage_count; ++i) {
      uint32_t size = pipeline->stages[i].stack_size;
      switch (pipeline->stages[i].stage) {
      case MESA_SHADER_RAYGEN:
         raygen_size = MAX2(raygen_size, size);
         break;
      case MESA_SHADER_CLOSEST_HIT:
      case MESA_SHADER_MISS:
         chit_miss_size = MAX2(chit_miss_size, size);
         break;
      case MESA_SHADER_CALLABLE:
         callable_size = MAX2(callable_size, size);
         break;
      case MESA_SHADER_INTERSECTION:
         intersection_size = MAX2(intersection_size, size);
         break;
      case MESA_SHADER_ANY_HIT:
         any_hit_size = MAX2(any_hit_size, size);
         break;
      default:
         UNREACHABLE("Invalid stage type in RT shader");
      }
   }
   pipeline->stack_size = raygen_size +
                          MIN2(pCreateInfo->maxPipelineRayRecursionDepth, 1) *
                             (chit_miss_size + intersection_size + any_hit_size + pipeline->traversal_stack_size) +
                          MAX2(0, (int)(pCreateInfo->maxPipelineRayRecursionDepth) - 1) * chit_miss_size +
                          2 * callable_size;
}

static void
combine_config(struct ac_shader_config *config, const struct ac_shader_config *other)
{
   config->num_sgprs = MAX2(config->num_sgprs, other->num_sgprs);
   config->num_vgprs = MAX2(config->num_vgprs, other->num_vgprs);
   config->num_shared_vgprs = MAX2(config->num_shared_vgprs, other->num_shared_vgprs);
   config->spilled_sgprs = MAX2(config->spilled_sgprs, other->spilled_sgprs);
   config->spilled_vgprs = MAX2(config->spilled_vgprs, other->spilled_vgprs);
   config->lds_size = MAX2(config->lds_size, other->lds_size);
   config->scratch_bytes_per_wave = MAX2(config->scratch_bytes_per_wave, other->scratch_bytes_per_wave);

   assert(config->float_mode == other->float_mode);
}

static void
postprocess_rt_config(struct ac_shader_config *config, const struct radeon_info *info, unsigned wave_size)
{
   config->rsrc1 =
      (config->rsrc1 & C_00B848_VGPRS) | S_00B848_VGPRS((config->num_vgprs - 1) / (wave_size == 32 ? 8 : 4));
   if (info->gfx_level < GFX10)
      config->rsrc1 = (config->rsrc1 & C_00B848_SGPRS) | S_00B848_SGPRS((config->num_sgprs - 1) / 8);

   unsigned lds_alloc = ac_shader_encode_lds_size(config->lds_size, info->gfx_level, MESA_SHADER_COMPUTE);
   config->rsrc2 = (config->rsrc2 & C_00B84C_LDS_SIZE) | S_00B84C_LDS_SIZE(lds_alloc);
   config->rsrc3 = (config->rsrc3 & C_00B8A0_SHARED_VGPR_CNT) | S_00B8A0_SHARED_VGPR_CNT(config->num_shared_vgprs / 8);
}

static void
compile_rt_prolog(struct radv_device *device, struct radv_ray_tracing_pipeline *pipeline)
{
   const bool uses_descriptor_heap = pipeline->base.base.create_flags & VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT;
   const struct radv_physical_device *pdev = radv_device_physical(device);
   uint32_t push_constant_size = 0;

   struct radv_shader_stage prolog_stage = {0};
   struct radv_shader_debug_info debug = {0};
   radv_build_rt_prolog(device, &prolog_stage, uses_descriptor_heap, &debug);
   prolog_stage.nir->options = &pdev->nir_options[MESA_SHADER_COMPUTE];
   radv_optimize_nir(prolog_stage.nir, false);
   radv_postprocess_nir(device, NULL, &prolog_stage);

   NIR_PASS(_, prolog_stage.nir, radv_nir_lower_call_abi, prolog_stage.info.wave_size);
   NIR_PASS(_, prolog_stage.nir, nir_lower_global_vars_to_local);
   NIR_PASS(_, prolog_stage.nir, nir_lower_vars_to_ssa);
   NIR_PASS(_, prolog_stage.nir, nir_opt_copy_prop);
   NIR_PASS(_, prolog_stage.nir, nir_opt_remove_phis);

   pipeline->prolog = radv_compile_rt_prolog(device, &prolog_stage, &debug);

   bool has_traversal = !!pipeline->base.base.shaders[MESA_SHADER_INTERSECTION];

   /* create combined config */
   struct ac_shader_config *config = &pipeline->prolog->config;
   for (unsigned i = 0; i < pipeline->stage_count; i++) {
      const struct radv_shader *shader = pipeline->stages[i].shader;

      /* !has_traversal means the pipeline is completely monolithic and will never call any
       * shaders (except for the raygen shader, which contains the entire RT pipeline).
       * Ignore all shaders except for the raygen shader - they may appear in the stages array due to pipeline library
       * imports, but they will never be called.
       */
      if (!has_traversal) {
         if (pipeline->stages[i].stage != MESA_SHADER_RAYGEN)
            continue;
      }

      if (!shader)
         continue;

      combine_config(config, &shader->config);

      push_constant_size = MAX2(push_constant_size, shader->info.push_constant_size);
   }

   for (unsigned i = 0; i < pipeline->group_count; i++) {
      const struct radv_shader *shader = pipeline->groups[i].ahit_isec_shader;

      if (!shader || !has_traversal)
         continue;

      combine_config(config, &shader->config);

      push_constant_size = MAX2(push_constant_size, shader->info.push_constant_size);
   }

   if (has_traversal) {
      const struct radv_shader *traversal_shader = pipeline->base.base.shaders[MESA_SHADER_INTERSECTION];

      combine_config(config, &traversal_shader->config);

      push_constant_size = MAX2(push_constant_size, traversal_shader->info.push_constant_size);
   }

   postprocess_rt_config(config, &pdev->info, pdev->rt_wave_size);

   pipeline->prolog->max_waves = radv_get_max_waves(device, config, &pipeline->prolog->info);
   pipeline->prolog->info.push_constant_size = push_constant_size;
}

void
radv_ray_tracing_pipeline_hash(const struct radv_device *device, const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                               const struct radv_ray_tracing_state_key *rt_state, unsigned char *hash)
{
   VK_FROM_HANDLE(radv_pipeline_layout, layout, pCreateInfo->layout);
   blake3_hasher ctx;

   _mesa_blake3_init(&ctx);
   radv_pipeline_hash(device, layout, &ctx);

   for (uint32_t i = 0; i < pCreateInfo->stageCount; i++) {
      _mesa_blake3_update(&ctx, rt_state->stages[i].blake3, sizeof(rt_state->stages[i].blake3));
   }

   for (uint32_t i = 0; i < pCreateInfo->groupCount; i++) {
      _mesa_blake3_update(&ctx, &pCreateInfo->pGroups[i].type, sizeof(pCreateInfo->pGroups[i].type));
      _mesa_blake3_update(&ctx, &pCreateInfo->pGroups[i].generalShader, sizeof(pCreateInfo->pGroups[i].generalShader));
      _mesa_blake3_update(&ctx, &pCreateInfo->pGroups[i].anyHitShader, sizeof(pCreateInfo->pGroups[i].anyHitShader));
      _mesa_blake3_update(&ctx, &pCreateInfo->pGroups[i].closestHitShader,
                        sizeof(pCreateInfo->pGroups[i].closestHitShader));
      _mesa_blake3_update(&ctx, &pCreateInfo->pGroups[i].intersectionShader,
                        sizeof(pCreateInfo->pGroups[i].intersectionShader));
      _mesa_blake3_update(&ctx, &rt_state->groups[i].handle, sizeof(struct radv_pipeline_group_handle));
   }

   if (pCreateInfo->pLibraryInfo) {
      for (uint32_t i = 0; i < pCreateInfo->pLibraryInfo->libraryCount; ++i) {
         VK_FROM_HANDLE(radv_pipeline, lib_pipeline, pCreateInfo->pLibraryInfo->pLibraries[i]);
         struct radv_ray_tracing_pipeline *lib = radv_pipeline_to_ray_tracing(lib_pipeline);
         _mesa_blake3_update(&ctx, lib->base.base.blake3, BLAKE3_KEY_LEN);
      }
   }

   const uint64_t pipeline_flags =
      vk_rt_pipeline_create_flags(pCreateInfo) &
      (VK_PIPELINE_CREATE_2_RAY_TRACING_SKIP_TRIANGLES_BIT_KHR | VK_PIPELINE_CREATE_2_RAY_TRACING_SKIP_AABBS_BIT_KHR |
       VK_PIPELINE_CREATE_2_RAY_TRACING_NO_NULL_ANY_HIT_SHADERS_BIT_KHR |
       VK_PIPELINE_CREATE_2_RAY_TRACING_NO_NULL_CLOSEST_HIT_SHADERS_BIT_KHR |
       VK_PIPELINE_CREATE_2_RAY_TRACING_NO_NULL_MISS_SHADERS_BIT_KHR |
       VK_PIPELINE_CREATE_2_RAY_TRACING_NO_NULL_INTERSECTION_SHADERS_BIT_KHR | VK_PIPELINE_CREATE_2_LIBRARY_BIT_KHR);
   _mesa_blake3_update(&ctx, &pipeline_flags, sizeof(pipeline_flags));

   _mesa_blake3_final(&ctx, hash);
}

static VkResult
radv_rt_pipeline_compile(struct radv_device *device, const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                         struct radv_ray_tracing_pipeline *pipeline, struct vk_pipeline_cache *cache,
                         const struct radv_ray_tracing_state_key *rt_state,
                         struct radv_serialized_shader_arena_block *capture_replay_blocks,
                         const VkPipelineCreationFeedbackCreateInfo *creation_feedback)
{
   bool skip_shaders_cache = radv_pipeline_skip_shaders_cache(device, &pipeline->base.base);
   const bool emit_ray_history = !!device->rra_trace.ray_history_buffer;
   VkPipelineCreationFeedback pipeline_feedback = {
      .flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT,
   };
   VkResult result = VK_SUCCESS;

   int64_t pipeline_start = os_time_get_nano();

   radv_ray_tracing_pipeline_hash(device, pCreateInfo, rt_state, pipeline->base.base.blake3);
   pipeline->base.base.pipeline_hash = *(uint64_t *)pipeline->base.base.blake3;

   /* Skip the shaders cache when any of the below are true:
    * - ray history is enabled
    * - group handles are saved and reused on a subsequent run (ie. capture/replay)
    */
   if (emit_ray_history || (pipeline->base.base.create_flags &
                            VK_PIPELINE_CREATE_2_RAY_TRACING_SHADER_GROUP_HANDLE_CAPTURE_REPLAY_BIT_KHR)) {
      skip_shaders_cache = true;
   }

   bool found_in_application_cache = true;
   if (!skip_shaders_cache &&
       radv_ray_tracing_pipeline_cache_search(device, cache, pipeline, &found_in_application_cache)) {
      if (found_in_application_cache)
         pipeline_feedback.flags |= VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT;
      result = VK_SUCCESS;
      goto done;
   }

   result = radv_rt_compile_shaders(device, cache, pCreateInfo, creation_feedback, rt_state->stage_keys, pipeline,
                                    capture_replay_blocks, skip_shaders_cache);

   if (result != VK_SUCCESS)
      return result;

   if (!skip_shaders_cache)
      radv_ray_tracing_pipeline_cache_insert(device, cache, pipeline, pCreateInfo->stageCount, pCreateInfo->groupCount);

done:
   pipeline_feedback.duration = os_time_get_nano() - pipeline_start;

   if (creation_feedback)
      *creation_feedback->pPipelineCreationFeedback = pipeline_feedback;

   return result;
}

void
radv_ray_tracing_state_key_finish(struct radv_ray_tracing_state_key *rt_state)
{
   free(rt_state->stages);
   free(rt_state->groups);
}

VkResult
radv_generate_ray_tracing_state_key(struct radv_device *device, const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                                    struct radv_ray_tracing_state_key *rt_state)
{
   VkResult result;

   memset(rt_state, 0, sizeof(*rt_state));

   /* Count the total number of stages/groups. */
   rt_state->stage_count = pCreateInfo->stageCount;
   rt_state->group_count = pCreateInfo->groupCount;

   if (pCreateInfo->pLibraryInfo) {
      for (unsigned i = 0; i < pCreateInfo->pLibraryInfo->libraryCount; ++i) {
         VK_FROM_HANDLE(radv_pipeline, pipeline, pCreateInfo->pLibraryInfo->pLibraries[i]);
         struct radv_ray_tracing_pipeline *library_pipeline = radv_pipeline_to_ray_tracing(pipeline);

         rt_state->stage_count += library_pipeline->stage_count;
         rt_state->group_count += library_pipeline->group_count;
      }
   }

   rt_state->stages = calloc(rt_state->stage_count, sizeof(*rt_state->stages));
   if (!rt_state->stages)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   rt_state->groups = calloc(rt_state->group_count, sizeof(*rt_state->groups));
   if (!rt_state->groups) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   /* Initialize stages/stage_keys/groups info. */
   radv_rt_fill_stage_info(pCreateInfo, rt_state->stages);

   radv_generate_rt_shaders_key(device, pCreateInfo, rt_state->stage_keys);

   VkPipelineCreateFlags2 create_flags = vk_rt_pipeline_create_flags(pCreateInfo);
   radv_init_rt_stage_hashes(device, create_flags, pCreateInfo, rt_state->stages, rt_state->stage_keys);

   result = radv_rt_fill_group_info(device, pCreateInfo, rt_state->stages, rt_state->groups);
   if (result != VK_SUCCESS)
      goto fail;

   return VK_SUCCESS;

fail:
   radv_ray_tracing_state_key_finish(rt_state);
   return result;
}

static VkResult
radv_ray_tracing_pipeline_import_binary(struct radv_device *device, struct radv_ray_tracing_pipeline *pipeline,
                                        const VkPipelineBinaryInfoKHR *binary_info)
{
   blake3_hash pipeline_hash;
   struct mesa_blake3 ctx;

   _mesa_blake3_init(&ctx);

   for (uint32_t i = 0; i < binary_info->binaryCount; i++) {
      VK_FROM_HANDLE(radv_pipeline_binary, pipeline_binary, binary_info->pPipelineBinaries[i]);
      struct radv_shader *shader;
      struct blob_reader blob;

      blob_reader_init(&blob, pipeline_binary->data, pipeline_binary->size);

      const struct radv_ray_tracing_binary_header *header =
         (const struct radv_ray_tracing_binary_header *)blob_read_bytes(&blob, sizeof(*header));

      if (header->is_traversal_shader) {
         shader = radv_shader_deserialize(device, pipeline_binary->key, sizeof(pipeline_binary->key), &blob);
         if (!shader)
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;

         pipeline->base.base.shaders[MESA_SHADER_INTERSECTION] = shader;

         _mesa_blake3_update(&ctx, pipeline_binary->key, sizeof(pipeline_binary->key));
         continue;
      }

      memcpy(&pipeline->stages[i].info, &header->stage_info, sizeof(pipeline->stages[i].info));
      pipeline->stages[i].stack_size = header->stack_size;

      if (header->has_shader) {
         shader = radv_shader_deserialize(device, pipeline_binary->key, sizeof(pipeline_binary->key), &blob);
         if (!shader)
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;

         pipeline->stages[i].shader = shader;

         _mesa_blake3_update(&ctx, pipeline_binary->key, sizeof(pipeline_binary->key));
      }

      if (header->has_nir) {
         nir_shader *nir = nir_deserialize(NULL, NULL, &blob);

         pipeline->stages[i].nir = radv_pipeline_cache_nir_to_handle(device, NULL, nir, header->stage_blake3, false);
         ralloc_free(nir);

         if (!pipeline->stages[i].nir)
            return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }

   _mesa_blake3_final(&ctx, pipeline_hash);

   pipeline->base.base.pipeline_hash = *(uint64_t *)pipeline_hash;

   return VK_SUCCESS;
}

static VkResult
radv_rt_pipeline_create(VkDevice _device, VkPipelineCache _cache, const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                        const VkAllocationCallbacks *pAllocator, VkPipeline *pPipeline)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   VK_FROM_HANDLE(vk_pipeline_cache, cache, _cache);
   VK_FROM_HANDLE(radv_pipeline_layout, pipeline_layout, pCreateInfo->layout);
   struct radv_ray_tracing_state_key rt_state;
   VkResult result;
   const VkPipelineCreationFeedbackCreateInfo *creation_feedback =
      vk_find_struct_const(pCreateInfo->pNext, PIPELINE_CREATION_FEEDBACK_CREATE_INFO);

   result = radv_generate_ray_tracing_state_key(device, pCreateInfo, &rt_state);
   if (result != VK_SUCCESS)
      return result;

   VK_MULTIALLOC(ma);
   VK_MULTIALLOC_DECL(&ma, struct radv_ray_tracing_pipeline, pipeline, 1);
   VK_MULTIALLOC_DECL(&ma, struct radv_ray_tracing_stage, stages, rt_state.stage_count);
   VK_MULTIALLOC_DECL(&ma, struct radv_ray_tracing_group, groups, rt_state.group_count);
   VK_MULTIALLOC_DECL(&ma, struct radv_serialized_shader_arena_block, capture_replay_blocks,
                      pCreateInfo->stageCount + pCreateInfo->groupCount);
   if (!vk_multialloc_zalloc2(&ma, &device->vk.alloc, pAllocator, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT)) {
      radv_ray_tracing_state_key_finish(&rt_state);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   radv_pipeline_init(device, &pipeline->base.base, RADV_PIPELINE_RAY_TRACING);
   pipeline->base.base.create_flags = vk_rt_pipeline_create_flags(pCreateInfo);
   pipeline->stage_count = rt_state.stage_count;
   pipeline->non_imported_stage_count = pCreateInfo->stageCount;
   pipeline->group_count = rt_state.group_count;
   pipeline->stages = stages;
   pipeline->groups = groups;

   memcpy(pipeline->stages, rt_state.stages, rt_state.stage_count * sizeof(struct radv_ray_tracing_stage));
   memcpy(pipeline->groups, rt_state.groups, rt_state.group_count * sizeof(struct radv_ray_tracing_group));

   /* cache robustness state for making merged shaders */
   if (rt_state.stage_keys[MESA_SHADER_INTERSECTION].storage_robustness2)
      pipeline->traversal_storage_robustness2 = true;

   if (rt_state.stage_keys[MESA_SHADER_INTERSECTION].uniform_robustness2)
      pipeline->traversal_uniform_robustness2 = true;

   result = radv_rt_init_capture_replay(device, pCreateInfo, stages, pipeline->groups, capture_replay_blocks);
   if (result != VK_SUCCESS)
      goto fail;

   const VkPipelineBinaryInfoKHR *binary_info = vk_find_struct_const(pCreateInfo->pNext, PIPELINE_BINARY_INFO_KHR);

   if (binary_info && binary_info->binaryCount > 0) {
      result = radv_ray_tracing_pipeline_import_binary(device, pipeline, binary_info);
   } else {
      result = radv_rt_pipeline_compile(device, pCreateInfo, pipeline, cache, &rt_state, capture_replay_blocks,
                                        creation_feedback);
      if (result != VK_SUCCESS)
         goto fail;
   }

   if (!(pipeline->base.base.create_flags & VK_PIPELINE_CREATE_2_LIBRARY_BIT_KHR)) {
      compute_rt_stack_size(pCreateInfo, pipeline);
      compile_rt_prolog(device, pipeline);

      radv_compute_pipeline_init(&pipeline->base, pipeline_layout, pipeline->prolog);
   }

   /* write shader VAs into group handles */
   for (unsigned i = 0; i < pipeline->group_count; i++) {
      if (pipeline->groups[i].recursive_shader != VK_SHADER_UNUSED_KHR) {
         struct radv_shader *shader = pipeline->stages[pipeline->groups[i].recursive_shader].shader;
         if (shader)
            pipeline->groups[i].handle.recursive_shader_ptr = shader->va;
      }
      if (pipeline->groups[i].ahit_isec_shader) {
         pipeline->groups[i].handle.ahit_isec_ptr = pipeline->groups[i].ahit_isec_shader->va;
      }
   }

   radv_pipeline_report_pso_history(device, &pipeline->base.base);

   *pPipeline = radv_pipeline_to_handle(&pipeline->base.base);
   radv_rmv_log_rt_pipeline_create(device, pipeline);

   radv_ray_tracing_state_key_finish(&rt_state);
   return result;

fail:
   radv_ray_tracing_state_key_finish(&rt_state);
   radv_pipeline_destroy(device, &pipeline->base.base, pAllocator);
   return result;
}

void
radv_destroy_ray_tracing_pipeline(struct radv_device *device, struct radv_ray_tracing_pipeline *pipeline)
{
   for (unsigned i = 0; i < pipeline->stage_count; i++) {
      if (pipeline->stages[i].nir)
         vk_pipeline_cache_object_unref(&device->vk, pipeline->stages[i].nir);
      if (pipeline->stages[i].shader)
         radv_shader_unref(device, pipeline->stages[i].shader);
   }
   for (unsigned i = 0; i < pipeline->group_count; i++) {
      if (pipeline->groups[i].ahit_isec_shader)
         radv_shader_unref(device, pipeline->groups[i].ahit_isec_shader);
   }

   if (pipeline->prolog)
      radv_shader_unref(device, pipeline->prolog);
   if (pipeline->base.base.shaders[MESA_SHADER_INTERSECTION])
      radv_shader_unref(device, pipeline->base.base.shaders[MESA_SHADER_INTERSECTION]);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateRayTracingPipelinesKHR(VkDevice _device, VkDeferredOperationKHR deferredOperation,
                                  VkPipelineCache pipelineCache, uint32_t count,
                                  const VkRayTracingPipelineCreateInfoKHR *pCreateInfos,
                                  const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines)
{
   VkResult result = VK_SUCCESS;

   unsigned i = 0;
   for (; i < count; i++) {
      VkResult r;
      r = radv_rt_pipeline_create(_device, pipelineCache, &pCreateInfos[i], pAllocator, &pPipelines[i]);
      if (r != VK_SUCCESS) {
         result = r;
         pPipelines[i] = VK_NULL_HANDLE;

         const VkPipelineCreateFlagBits2 create_flags = vk_rt_pipeline_create_flags(&pCreateInfos[i]);
         if (create_flags & VK_PIPELINE_CREATE_2_EARLY_RETURN_ON_FAILURE_BIT)
            break;
      }
   }

   for (; i < count; ++i)
      pPipelines[i] = VK_NULL_HANDLE;

   if (result != VK_SUCCESS)
      return result;

   /* Work around Portal RTX not handling VK_OPERATION_NOT_DEFERRED_KHR correctly. */
   if (deferredOperation != VK_NULL_HANDLE)
      return VK_OPERATION_DEFERRED_KHR;

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetRayTracingShaderGroupHandlesKHR(VkDevice device, VkPipeline _pipeline, uint32_t firstGroup, uint32_t groupCount,
                                        size_t dataSize, void *pData)
{
   VK_FROM_HANDLE(radv_pipeline, pipeline, _pipeline);
   struct radv_ray_tracing_group *groups = radv_pipeline_to_ray_tracing(pipeline)->groups;
   char *data = pData;

   STATIC_ASSERT(sizeof(struct radv_pipeline_group_handle) <= RADV_RT_HANDLE_SIZE);

   memset(data, 0, groupCount * RADV_RT_HANDLE_SIZE);

   for (uint32_t i = 0; i < groupCount; ++i) {
      memcpy(data + i * RADV_RT_HANDLE_SIZE, &groups[firstGroup + i].handle, sizeof(struct radv_pipeline_group_handle));
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkDeviceSize VKAPI_CALL
radv_GetRayTracingShaderGroupStackSizeKHR(VkDevice device, VkPipeline _pipeline, uint32_t group,
                                          VkShaderGroupShaderKHR groupShader)
{
   VK_FROM_HANDLE(radv_pipeline, pipeline, _pipeline);
   struct radv_ray_tracing_pipeline *rt_pipeline = radv_pipeline_to_ray_tracing(pipeline);
   struct radv_ray_tracing_group *rt_group = &rt_pipeline->groups[group];

   struct radv_ray_tracing_stage *shader_stage;

   switch (groupShader) {
   case VK_SHADER_GROUP_SHADER_GENERAL_KHR:
   case VK_SHADER_GROUP_SHADER_CLOSEST_HIT_KHR:
      shader_stage = &rt_pipeline->stages[rt_group->recursive_shader];
      break;
   case VK_SHADER_GROUP_SHADER_ANY_HIT_KHR:
      /* If the any-hit shader is inlined into an intersection shader, there is no stack specific to the any-hit shader
       * and all stack will be allocated for the intersection shader instead.
       */
      if (rt_group->intersection_shader != VK_SHADER_UNUSED_KHR)
         return 0;
      shader_stage = &rt_pipeline->stages[rt_group->any_hit_shader];
      break;
   case VK_SHADER_GROUP_SHADER_INTERSECTION_KHR:
      shader_stage = &rt_pipeline->stages[rt_group->intersection_shader];
      break;
   default:
      return 0;
   }

   uint32_t stack_size = shader_stage->stack_size;
   /* Applications need to allocate stack for the traversal shader, too. The API doesn't intend for a constant
    * traversal stack size, so add the stack size to every shader potentially called by the traversal shader.
    * Applications are expected to max() shader stages together, so this shouldn't result in any unnecessary stack
    * usage.
    */
   if (shader_stage->stage == MESA_SHADER_CLOSEST_HIT || shader_stage->stage == MESA_SHADER_ANY_HIT ||
       shader_stage->stage == MESA_SHADER_INTERSECTION || shader_stage->stage == MESA_SHADER_MISS)
      stack_size += rt_pipeline->traversal_stack_size;
   return stack_size;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetRayTracingCaptureReplayShaderGroupHandlesKHR(VkDevice device, VkPipeline _pipeline, uint32_t firstGroup,
                                                     uint32_t groupCount, size_t dataSize, void *pData)
{
   VK_FROM_HANDLE(radv_pipeline, pipeline, _pipeline);
   struct radv_ray_tracing_pipeline *rt_pipeline = radv_pipeline_to_ray_tracing(pipeline);
   struct radv_rt_capture_replay_handle *data = pData;

   memset(data, 0, groupCount * sizeof(struct radv_rt_capture_replay_handle));

   for (uint32_t i = 0; i < groupCount; ++i) {
      uint32_t recursive_shader = rt_pipeline->groups[firstGroup + i].recursive_shader;
      if (recursive_shader != VK_SHADER_UNUSED_KHR) {
         struct radv_shader *shader = rt_pipeline->stages[recursive_shader].shader;
         if (shader) {
            data[i].recursive_shader_alloc.offset = shader->alloc->offset;
            data[i].recursive_shader_alloc.size = shader->alloc->size;
            data[i].recursive_shader_alloc.arena_va = radv_buffer_get_va(shader->alloc->arena->bo);
            data[i].recursive_shader_alloc.arena_size = shader->alloc->arena->size;
         }
      }
      if (rt_pipeline->groups[firstGroup + i].ahit_isec_shader) {
         struct radv_shader *shader = rt_pipeline->groups[firstGroup + i].ahit_isec_shader;
         data[i].ahit_isec_alloc.offset = shader->alloc->offset;
         data[i].ahit_isec_alloc.size = shader->alloc->size;
         data[i].ahit_isec_alloc.arena_va = radv_buffer_get_va(shader->alloc->arena->bo);
         data[i].ahit_isec_alloc.arena_size = shader->alloc->arena->size;
      }
      data[i].non_recursive_idx = rt_pipeline->groups[firstGroup + i].handle.any_hit_index;
   }

   return VK_SUCCESS;
}
