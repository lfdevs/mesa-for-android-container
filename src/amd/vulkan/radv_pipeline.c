/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
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

#include "meta/radv_meta.h"
#include "nir/nir.h"
#include "nir/nir_builder.h"
#include "nir/nir_serialize.h"
#include "nir/nir_vulkan.h"
#include "nir/radv_nir.h"
#include "spirv/nir_spirv.h"
#include "util/disk_cache.h"
#include "util/mesa-sha1.h"
#include "util/os_time.h"
#include "util/u_atomic.h"
#include "radv_cs.h"
#include "radv_debug.h"
#include "radv_private.h"
#include "radv_shader.h"
#include "radv_shader_args.h"
#include "vk_pipeline.h"
#include "vk_render_pass.h"
#include "vk_util.h"

#include "util/u_debug.h"
#include "ac_binary.h"
#include "ac_nir.h"
#include "ac_shader_util.h"
#include "aco_interface.h"
#include "sid.h"
#include "vk_format.h"

bool
radv_shader_need_indirect_descriptor_sets(const struct radv_shader *shader)
{
   const struct radv_userdata_info *loc = radv_get_user_sgpr(shader, AC_UD_INDIRECT_DESCRIPTOR_SETS);
   return loc->sgpr_idx != -1;
}

bool
radv_pipeline_capture_shaders(const struct radv_device *device, VkPipelineCreateFlags flags)
{
   return (flags & VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR) ||
          (device->instance->debug_flags & RADV_DEBUG_DUMP_SHADERS) || device->keep_shader_info;
}

bool
radv_pipeline_capture_shader_stats(const struct radv_device *device, VkPipelineCreateFlags flags)
{
   return (flags & VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR) ||
          (device->instance->debug_flags & RADV_DEBUG_DUMP_SHADER_STATS) ||
          device->keep_shader_info;
}

void
radv_pipeline_init(struct radv_device *device, struct radv_pipeline *pipeline,
                    enum radv_pipeline_type type)
{
   vk_object_base_init(&device->vk, &pipeline->base, VK_OBJECT_TYPE_PIPELINE);

   pipeline->type = type;
}

void
radv_pipeline_destroy(struct radv_device *device, struct radv_pipeline *pipeline,
                      const VkAllocationCallbacks *allocator)
{
   switch (pipeline->type) {
   case RADV_PIPELINE_GRAPHICS:
      radv_destroy_graphics_pipeline(device, radv_pipeline_to_graphics(pipeline));
      break;
   case RADV_PIPELINE_GRAPHICS_LIB:
      radv_destroy_graphics_lib_pipeline(device, radv_pipeline_to_graphics_lib(pipeline));
      break;
   case RADV_PIPELINE_COMPUTE:
      radv_destroy_compute_pipeline(device, radv_pipeline_to_compute(pipeline));
      break;
   case RADV_PIPELINE_RAY_TRACING_LIB:
      radv_destroy_ray_tracing_lib_pipeline(device, radv_pipeline_to_ray_tracing_lib(pipeline));
      break;
   case RADV_PIPELINE_RAY_TRACING:
      radv_destroy_ray_tracing_pipeline(device, radv_pipeline_to_ray_tracing(pipeline));
      break;
   default:
      unreachable("invalid pipeline type");
   }

   if (pipeline->cs.buf)
      free(pipeline->cs.buf);

   radv_rmv_log_resource_destroy(device, (uint64_t)radv_pipeline_to_handle(pipeline));
   vk_object_base_finish(&pipeline->base);
   vk_free2(&device->vk.alloc, allocator, pipeline);
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroyPipeline(VkDevice _device, VkPipeline _pipeline,
                     const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_pipeline, pipeline, _pipeline);

   if (!_pipeline)
      return;

   radv_pipeline_destroy(device, pipeline, pAllocator);
}

void
radv_pipeline_init_scratch(const struct radv_device *device, struct radv_pipeline *pipeline)
{
   unsigned scratch_bytes_per_wave = 0;
   unsigned max_waves = 0;
   bool is_rt = pipeline->type == RADV_PIPELINE_RAY_TRACING;

   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i) {
      if (pipeline->shaders[i] && (pipeline->shaders[i]->config.scratch_bytes_per_wave || is_rt)) {
         unsigned max_stage_waves = device->scratch_waves;

         scratch_bytes_per_wave =
            MAX2(scratch_bytes_per_wave, pipeline->shaders[i]->config.scratch_bytes_per_wave);

         max_stage_waves =
            MIN2(max_stage_waves, 4 * device->physical_device->rad_info.num_cu *
                                     radv_get_max_waves(device, pipeline->shaders[i], i));
         max_waves = MAX2(max_waves, max_stage_waves);
      }
   }

   pipeline->scratch_bytes_per_wave = scratch_bytes_per_wave;
   pipeline->max_waves = max_waves;
}

struct radv_pipeline_key
radv_generate_pipeline_key(const struct radv_device *device, const struct radv_pipeline *pipeline,
                           VkPipelineCreateFlags flags)
{
   struct radv_pipeline_key key;

   memset(&key, 0, sizeof(key));

   if (flags & VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT)
      key.optimisations_disabled = 1;

   key.disable_aniso_single_level = device->instance->disable_aniso_single_level &&
                                    device->physical_device->rad_info.gfx_level < GFX8;

   key.image_2d_view_of_3d =
      device->image_2d_view_of_3d && device->physical_device->rad_info.gfx_level == GFX9;

   key.tex_non_uniform = device->instance->tex_non_uniform;

   return key;
}

uint32_t
radv_get_hash_flags(const struct radv_device *device, bool stats)
{
   uint32_t hash_flags = 0;

   if (device->physical_device->use_ngg_culling)
      hash_flags |= RADV_HASH_SHADER_USE_NGG_CULLING;
   if (device->instance->perftest_flags & RADV_PERFTEST_EMULATE_RT)
      hash_flags |= RADV_HASH_SHADER_EMULATE_RT;
   if (device->physical_device->rt_wave_size == 64)
      hash_flags |= RADV_HASH_SHADER_RT_WAVE64;
   if (device->physical_device->cs_wave_size == 32)
      hash_flags |= RADV_HASH_SHADER_CS_WAVE32;
   if (device->physical_device->ps_wave_size == 32)
      hash_flags |= RADV_HASH_SHADER_PS_WAVE32;
   if (device->physical_device->ge_wave_size == 32)
      hash_flags |= RADV_HASH_SHADER_GE_WAVE32;
   if (device->physical_device->use_llvm)
      hash_flags |= RADV_HASH_SHADER_LLVM;
   if (stats)
      hash_flags |= RADV_HASH_SHADER_KEEP_STATISTICS;
   if (device->robust_buffer_access) /* forces per-attribute vertex descriptors */
      hash_flags |= RADV_HASH_SHADER_ROBUST_BUFFER_ACCESS;
   if (device->robust_buffer_access2) /* affects load/store vectorizer */
      hash_flags |= RADV_HASH_SHADER_ROBUST_BUFFER_ACCESS2;
   if (device->instance->debug_flags & RADV_DEBUG_SPLIT_FMA)
      hash_flags |= RADV_HASH_SHADER_SPLIT_FMA;
   if (device->instance->debug_flags & RADV_DEBUG_NO_FMASK)
      hash_flags |= RADV_HASH_SHADER_NO_FMASK;
   if (device->physical_device->use_ngg_streamout)
      hash_flags |= RADV_HASH_SHADER_NGG_STREAMOUT;
   return hash_flags;
}

void
radv_pipeline_stage_init(const VkPipelineShaderStageCreateInfo *sinfo,
                         struct radv_pipeline_stage *out_stage, gl_shader_stage stage)
{
   const VkShaderModuleCreateInfo *minfo =
      vk_find_struct_const(sinfo->pNext, SHADER_MODULE_CREATE_INFO);
   const VkPipelineShaderStageModuleIdentifierCreateInfoEXT *iinfo =
      vk_find_struct_const(sinfo->pNext, PIPELINE_SHADER_STAGE_MODULE_IDENTIFIER_CREATE_INFO_EXT);

   if (sinfo->module == VK_NULL_HANDLE && !minfo && !iinfo)
      return;

   memset(out_stage, 0, sizeof(*out_stage));

   out_stage->stage = stage;
   out_stage->entrypoint = sinfo->pName;
   out_stage->spec_info = sinfo->pSpecializationInfo;
   out_stage->feedback.flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT;

   if (sinfo->module != VK_NULL_HANDLE) {
      struct vk_shader_module *module = vk_shader_module_from_handle(sinfo->module);

      out_stage->spirv.data = module->data;
      out_stage->spirv.size = module->size;
      out_stage->spirv.object = &module->base;

      if (module->nir)
         out_stage->internal_nir = module->nir;
   } else if (minfo) {
      out_stage->spirv.data = (const char *)minfo->pCode;
      out_stage->spirv.size = minfo->codeSize;
   }

   vk_pipeline_hash_shader_stage(sinfo, NULL, out_stage->shader_sha1);
}

static const struct vk_ycbcr_conversion_state *
ycbcr_conversion_lookup(const void *data, uint32_t set, uint32_t binding, uint32_t array_index)
{
   const struct radv_pipeline_layout *layout = data;

   const struct radv_descriptor_set_layout *set_layout = layout->set[set].layout;
   const struct vk_ycbcr_conversion_state *ycbcr_samplers =
      radv_immutable_ycbcr_samplers(set_layout, binding);

   if (!ycbcr_samplers)
      return NULL;

   return ycbcr_samplers + array_index;
}

bool
radv_mem_vectorize_callback(unsigned align_mul, unsigned align_offset, unsigned bit_size,
                            unsigned num_components, nir_intrinsic_instr *low,
                            nir_intrinsic_instr *high, void *data)
{
   if (num_components > 4)
      return false;

   /* >128 bit loads are split except with SMEM */
   if (bit_size * num_components > 128)
      return false;

   uint32_t align;
   if (align_offset)
      align = 1 << (ffs(align_offset) - 1);
   else
      align = align_mul;

   switch (low->intrinsic) {
   case nir_intrinsic_load_global:
   case nir_intrinsic_store_global:
   case nir_intrinsic_store_ssbo:
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_push_constant: {
      unsigned max_components;
      if (align % 4 == 0)
         max_components = NIR_MAX_VEC_COMPONENTS;
      else if (align % 2 == 0)
         max_components = 16u / bit_size;
      else
         max_components = 8u / bit_size;
      return (align % (bit_size / 8u)) == 0 && num_components <= max_components;
   }
   case nir_intrinsic_load_deref:
   case nir_intrinsic_store_deref:
      assert(nir_deref_mode_is(nir_src_as_deref(low->src[0]), nir_var_mem_shared));
      FALLTHROUGH;
   case nir_intrinsic_load_shared:
   case nir_intrinsic_store_shared:
      if (bit_size * num_components ==
          96) { /* 96 bit loads require 128 bit alignment and are split otherwise */
         return align % 16 == 0;
      } else if (bit_size == 16 && (align % 4)) {
         /* AMD hardware can't do 2-byte aligned f16vec2 loads, but they are useful for ALU
          * vectorization, because our vectorizer requires the scalar IR to already contain vectors.
          */
         return (align % 2 == 0) && num_components <= 2;
      } else {
         if (num_components == 3) {
            /* AMD hardware can't do 3-component loads except for 96-bit loads, handled above. */
            return false;
         }
         unsigned req = bit_size * num_components;
         if (req == 64 || req == 128) /* 64-bit and 128-bit loads can use ds_read2_b{32,64} */
            req /= 2u;
         return align % (req / 8u) == 0;
      }
   default:
      return false;
   }
   return false;
}

static unsigned
lower_bit_size_callback(const nir_instr *instr, void *_)
{
   struct radv_device *device = _;
   enum amd_gfx_level chip = device->physical_device->rad_info.gfx_level;

   if (instr->type != nir_instr_type_alu)
      return 0;
   nir_alu_instr *alu = nir_instr_as_alu(instr);

   /* If an instruction is not scalarized by this point,
    * it can be emitted as packed instruction */
   if (alu->dest.dest.ssa.num_components > 1)
      return 0;

   if (alu->dest.dest.ssa.bit_size & (8 | 16)) {
      unsigned bit_size = alu->dest.dest.ssa.bit_size;
      switch (alu->op) {
      case nir_op_bitfield_select:
      case nir_op_imul_high:
      case nir_op_umul_high:
      case nir_op_uadd_carry:
      case nir_op_usub_borrow:
         return 32;
      case nir_op_iabs:
      case nir_op_imax:
      case nir_op_umax:
      case nir_op_imin:
      case nir_op_umin:
      case nir_op_ishr:
      case nir_op_ushr:
      case nir_op_ishl:
      case nir_op_isign:
      case nir_op_uadd_sat:
      case nir_op_usub_sat:
         return (bit_size == 8 || !(chip >= GFX8 && nir_dest_is_divergent(alu->dest.dest))) ? 32
                                                                                            : 0;
      case nir_op_iadd_sat:
      case nir_op_isub_sat:
         return bit_size == 8 || !nir_dest_is_divergent(alu->dest.dest) ? 32 : 0;

      default:
         return 0;
      }
   }

   if (nir_src_bit_size(alu->src[0].src) & (8 | 16)) {
      unsigned bit_size = nir_src_bit_size(alu->src[0].src);
      switch (alu->op) {
      case nir_op_bit_count:
      case nir_op_find_lsb:
      case nir_op_ufind_msb:
         return 32;
      case nir_op_ilt:
      case nir_op_ige:
      case nir_op_ieq:
      case nir_op_ine:
      case nir_op_ult:
      case nir_op_uge:
         return (bit_size == 8 || !(chip >= GFX8 && nir_dest_is_divergent(alu->dest.dest))) ? 32
                                                                                            : 0;
      default:
         return 0;
      }
   }

   return 0;
}

static uint8_t
opt_vectorize_callback(const nir_instr *instr, const void *_)
{
   if (instr->type != nir_instr_type_alu)
      return 0;

   const struct radv_device *device = _;
   enum amd_gfx_level chip = device->physical_device->rad_info.gfx_level;
   if (chip < GFX9)
      return 1;

   const nir_alu_instr *alu = nir_instr_as_alu(instr);
   const unsigned bit_size = alu->dest.dest.ssa.bit_size;
   if (bit_size != 16)
      return 1;

   switch (alu->op) {
   case nir_op_fadd:
   case nir_op_fsub:
   case nir_op_fmul:
   case nir_op_ffma:
   case nir_op_fdiv:
   case nir_op_flrp:
   case nir_op_fabs:
   case nir_op_fneg:
   case nir_op_fsat:
   case nir_op_fmin:
   case nir_op_fmax:
   case nir_op_iabs:
   case nir_op_iadd:
   case nir_op_iadd_sat:
   case nir_op_uadd_sat:
   case nir_op_isub:
   case nir_op_isub_sat:
   case nir_op_usub_sat:
   case nir_op_ineg:
   case nir_op_imul:
   case nir_op_imin:
   case nir_op_imax:
   case nir_op_umin:
   case nir_op_umax:
      return 2;
   case nir_op_ishl: /* TODO: in NIR, these have 32bit shift operands */
   case nir_op_ishr: /* while Radeon needs 16bit operands when vectorized */
   case nir_op_ushr:
   default:
      return 1;
   }
}

static nir_component_mask_t
non_uniform_access_callback(const nir_src *src, void *_)
{
   if (src->ssa->num_components == 1)
      return 0x1;
   return nir_chase_binding(*src).success ? 0x2 : 0x3;
}

void
radv_postprocess_nir(struct radv_device *device, const struct radv_pipeline_layout *pipeline_layout,
                     const struct radv_pipeline_key *pipeline_key, unsigned last_vgt_api_stage,
                     struct radv_pipeline_stage *stage)
{
   enum amd_gfx_level gfx_level = device->physical_device->rad_info.gfx_level;
   bool progress;

   /* Wave and workgroup size should already be filled. */
   assert(stage->info.wave_size && stage->info.workgroup_size);

   if (stage->stage == MESA_SHADER_FRAGMENT) {
      if (!pipeline_key->optimisations_disabled) {
         NIR_PASS(_, stage->nir, nir_opt_cse);
      }
      NIR_PASS(_, stage->nir, radv_nir_lower_fs_intrinsics, stage, pipeline_key);
   }

   enum nir_lower_non_uniform_access_type lower_non_uniform_access_types =
      nir_lower_non_uniform_ubo_access | nir_lower_non_uniform_ssbo_access |
      nir_lower_non_uniform_texture_access | nir_lower_non_uniform_image_access;

   /* In practice, most shaders do not have non-uniform-qualified
    * accesses (see
    * https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/17558#note_1475069)
    * thus a cheaper and likely to fail check is run first.
    */
   if (nir_has_non_uniform_access(stage->nir, lower_non_uniform_access_types)) {
      if (!pipeline_key->optimisations_disabled) {
         NIR_PASS(_, stage->nir, nir_opt_non_uniform_access);
      }

      if (!radv_use_llvm_for_stage(device, stage->stage)) {
         nir_lower_non_uniform_access_options options = {
            .types = lower_non_uniform_access_types,
            .callback = &non_uniform_access_callback,
            .callback_data = NULL,
         };
         NIR_PASS(_, stage->nir, nir_lower_non_uniform_access, &options);
      }
   }
   NIR_PASS(_, stage->nir, nir_lower_memory_model);

   nir_load_store_vectorize_options vectorize_opts = {
      .modes = nir_var_mem_ssbo | nir_var_mem_ubo | nir_var_mem_push_const | nir_var_mem_shared |
               nir_var_mem_global,
      .callback = radv_mem_vectorize_callback,
      .robust_modes = 0,
      /* On GFX6, read2/write2 is out-of-bounds if the offset register is negative, even if
       * the final offset is not.
       */
      .has_shared2_amd = gfx_level >= GFX7,
   };

   if (device->robust_buffer_access2) {
      vectorize_opts.robust_modes = nir_var_mem_ubo | nir_var_mem_ssbo | nir_var_mem_push_const;
   }

   if (!pipeline_key->optimisations_disabled) {
      progress = false;
      NIR_PASS(progress, stage->nir, nir_opt_load_store_vectorize, &vectorize_opts);
      if (progress) {
         NIR_PASS(_, stage->nir, nir_copy_prop);
         NIR_PASS(_, stage->nir, nir_opt_shrink_stores,
                  !device->instance->disable_shrink_image_store);

         /* Gather info again, to update whether 8/16-bit are used. */
         nir_shader_gather_info(stage->nir, nir_shader_get_entrypoint(stage->nir));
      }
   }

   NIR_PASS(_, stage->nir, ac_nir_lower_subdword_loads,
            (ac_nir_lower_subdword_options){.modes_1_comp = nir_var_mem_ubo,
                                            .modes_N_comps = nir_var_mem_ubo | nir_var_mem_ssbo});

   progress = false;
   NIR_PASS(progress, stage->nir, nir_vk_lower_ycbcr_tex, ycbcr_conversion_lookup, pipeline_layout);
   /* Gather info in the case that nir_vk_lower_ycbcr_tex might have emitted resinfo instructions. */
   if (progress)
      nir_shader_gather_info(stage->nir, nir_shader_get_entrypoint(stage->nir));

   if (stage->nir->info.uses_resource_info_query)
      NIR_PASS(_, stage->nir, ac_nir_lower_resinfo, gfx_level);

   NIR_PASS_V(stage->nir, radv_nir_apply_pipeline_layout, device, pipeline_layout, &stage->info,
              &stage->args);

   if (!pipeline_key->optimisations_disabled) {
      NIR_PASS(_, stage->nir, nir_opt_shrink_vectors);
   }

   NIR_PASS(_, stage->nir, nir_lower_alu_width, opt_vectorize_callback, device);

   /* lower ALU operations */
   NIR_PASS(_, stage->nir, nir_lower_int64);

   nir_move_options sink_opts = nir_move_const_undef | nir_move_copies;

   if (!pipeline_key->optimisations_disabled) {
      if (stage->stage != MESA_SHADER_FRAGMENT || !pipeline_key->disable_sinking_load_input_fs)
         sink_opts |= nir_move_load_input;

      NIR_PASS(_, stage->nir, nir_opt_sink, sink_opts);
      NIR_PASS(_, stage->nir, nir_opt_move,
               nir_move_load_input | nir_move_const_undef | nir_move_copies);
   }

   /* Lower VS inputs. We need to do this after nir_opt_sink, because
    * load_input can be reordered, but buffer loads can't.
    */
   if (stage->stage == MESA_SHADER_VERTEX) {
      NIR_PASS(_, stage->nir, radv_nir_lower_vs_inputs, stage, pipeline_key,
               &device->physical_device->rad_info);
   }

   /* Lower I/O intrinsics to memory instructions. */
   bool io_to_mem = radv_nir_lower_io_to_mem(device, stage);
   bool lowered_ngg = stage->info.is_ngg && stage->stage == last_vgt_api_stage;
   if (lowered_ngg)
      radv_lower_ngg(device, stage, pipeline_key);

   if (stage->stage == last_vgt_api_stage && !lowered_ngg) {
      if (stage->stage != MESA_SHADER_GEOMETRY) {
         NIR_PASS_V(stage->nir, ac_nir_lower_legacy_vs, gfx_level,
                    stage->info.outinfo.clip_dist_mask | stage->info.outinfo.cull_dist_mask,
                    stage->info.outinfo.vs_output_param_offset, stage->info.outinfo.param_exports,
                    stage->info.outinfo.export_prim_id, false, false,
                    stage->info.force_vrs_per_vertex);

      } else {
         ac_nir_gs_output_info gs_out_info = {
            .streams = stage->info.gs.output_streams,
            .usage_mask = stage->info.gs.output_usage_mask,
         };
         NIR_PASS_V(stage->nir, ac_nir_lower_legacy_gs, false, false, &gs_out_info);
      }
   }

   NIR_PASS(_, stage->nir, nir_opt_idiv_const, 8);

   NIR_PASS(_, stage->nir, nir_lower_idiv,
            &(nir_lower_idiv_options){
               .allow_fp16 = gfx_level >= GFX9,
            });

   if (radv_use_llvm_for_stage(device, stage->stage))
      NIR_PASS_V(stage->nir, nir_lower_io_to_scalar, nir_var_mem_global);

   NIR_PASS(_, stage->nir, ac_nir_lower_global_access);
   NIR_PASS_V(stage->nir, radv_nir_lower_abi, gfx_level, &stage->info, &stage->args, pipeline_key,
              device->physical_device->rad_info.address32_hi);
   radv_optimize_nir_algebraic(stage->nir, io_to_mem || lowered_ngg ||
                                              stage->stage == MESA_SHADER_COMPUTE ||
                                              stage->stage == MESA_SHADER_TASK);

   if (stage->nir->info.bit_sizes_int & (8 | 16)) {
      if (gfx_level >= GFX8) {
         NIR_PASS(_, stage->nir, nir_convert_to_lcssa, true, true);
         nir_divergence_analysis(stage->nir);
      }

      if (nir_lower_bit_size(stage->nir, lower_bit_size_callback, device)) {
         NIR_PASS(_, stage->nir, nir_opt_constant_folding);
      }

      if (gfx_level >= GFX8)
         NIR_PASS(_, stage->nir, nir_opt_remove_phis); /* cleanup LCSSA phis */
   }
   if (((stage->nir->info.bit_sizes_int | stage->nir->info.bit_sizes_float) & 16) &&
       gfx_level >= GFX9) {
      bool separate_g16 = gfx_level >= GFX10;
      struct nir_fold_tex_srcs_options fold_srcs_options[] = {
         {
            .sampler_dims =
               ~(BITFIELD_BIT(GLSL_SAMPLER_DIM_CUBE) | BITFIELD_BIT(GLSL_SAMPLER_DIM_BUF)),
            .src_types = (1 << nir_tex_src_coord) | (1 << nir_tex_src_lod) |
                         (1 << nir_tex_src_bias) | (1 << nir_tex_src_min_lod) |
                         (1 << nir_tex_src_ms_index) |
                         (separate_g16 ? 0 : (1 << nir_tex_src_ddx) | (1 << nir_tex_src_ddy)),
         },
         {
            .sampler_dims = ~BITFIELD_BIT(GLSL_SAMPLER_DIM_CUBE),
            .src_types = (1 << nir_tex_src_ddx) | (1 << nir_tex_src_ddy),
         },
      };
      struct nir_fold_16bit_tex_image_options fold_16bit_options = {
         .rounding_mode = nir_rounding_mode_rtz,
         .fold_tex_dest_types = nir_type_float,
         .fold_image_dest_types = nir_type_float,
         .fold_image_store_data = true,
         .fold_image_srcs = !radv_use_llvm_for_stage(device, stage->stage),
         .fold_srcs_options_count = separate_g16 ? 2 : 1,
         .fold_srcs_options = fold_srcs_options,
      };
      NIR_PASS(_, stage->nir, nir_fold_16bit_tex_image, &fold_16bit_options);

      if (!pipeline_key->optimisations_disabled) {
         NIR_PASS(_, stage->nir, nir_opt_vectorize, opt_vectorize_callback, device);
      }
   }

   /* cleanup passes */
   NIR_PASS(_, stage->nir, nir_lower_alu_width, opt_vectorize_callback, device);
   NIR_PASS(_, stage->nir, nir_lower_load_const_to_scalar);
   NIR_PASS(_, stage->nir, nir_copy_prop);
   NIR_PASS(_, stage->nir, nir_opt_dce);

   if (!pipeline_key->optimisations_disabled) {
      sink_opts |= nir_move_comparisons | nir_move_load_ubo | nir_move_load_ssbo;
      NIR_PASS(_, stage->nir, nir_opt_sink, sink_opts);

      nir_move_options move_opts = nir_move_const_undef | nir_move_load_ubo | nir_move_load_input |
                                   nir_move_comparisons | nir_move_copies;
      NIR_PASS(_, stage->nir, nir_opt_move, move_opts);
   }
}

static uint32_t
radv_get_executable_count(struct radv_pipeline *pipeline)
{
   if (pipeline->type == RADV_PIPELINE_RAY_TRACING)
      return 1;

   uint32_t ret = 0;
   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i) {
      if (!pipeline->shaders[i])
         continue;

      if (i == MESA_SHADER_GEOMETRY &&
          !radv_pipeline_has_ngg(radv_pipeline_to_graphics(pipeline))) {
         ret += 2u;
      } else {
         ret += 1u;
      }
   }
   return ret;
}

static struct radv_shader *
radv_get_shader_from_executable_index(struct radv_pipeline *pipeline, int index,
                                      gl_shader_stage *stage)
{
   if (pipeline->type == RADV_PIPELINE_RAY_TRACING) {
      *stage = MESA_SHADER_RAYGEN;
      return pipeline->shaders[*stage];
   }

   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i) {
      if (!pipeline->shaders[i])
         continue;
      if (!index) {
         *stage = i;
         return pipeline->shaders[i];
      }

      --index;

      if (i == MESA_SHADER_GEOMETRY &&
          !radv_pipeline_has_ngg(radv_pipeline_to_graphics(pipeline))) {
         if (!index) {
            *stage = i;
            return pipeline->gs_copy_shader;
         }
         --index;
      }
   }

   *stage = -1;
   return NULL;
}

/* Basically strlcpy (which does not exist on linux) specialized for
 * descriptions. */
static void
desc_copy(char *desc, const char *src)
{
   int len = strlen(src);
   assert(len < VK_MAX_DESCRIPTION_SIZE);
   memcpy(desc, src, len);
   memset(desc + len, 0, VK_MAX_DESCRIPTION_SIZE - len);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetPipelineExecutablePropertiesKHR(VkDevice _device, const VkPipelineInfoKHR *pPipelineInfo,
                                        uint32_t *pExecutableCount,
                                        VkPipelineExecutablePropertiesKHR *pProperties)
{
   RADV_FROM_HANDLE(radv_pipeline, pipeline, pPipelineInfo->pipeline);
   const uint32_t total_count = radv_get_executable_count(pipeline);

   if (!pProperties) {
      *pExecutableCount = total_count;
      return VK_SUCCESS;
   }

   const uint32_t count = MIN2(total_count, *pExecutableCount);
   for (unsigned i = 0, executable_idx = 0; i < MESA_VULKAN_SHADER_STAGES && executable_idx < count; ++i) {
      if (!pipeline->shaders[i])
         continue;
      pProperties[executable_idx].stages = mesa_to_vk_shader_stage(i);
      const char *name = NULL;
      const char *description = NULL;
      switch (i) {
      case MESA_SHADER_VERTEX:
         name = "Vertex Shader";
         description = "Vulkan Vertex Shader";
         break;
      case MESA_SHADER_TESS_CTRL:
         if (!pipeline->shaders[MESA_SHADER_VERTEX]) {
            pProperties[executable_idx].stages |= VK_SHADER_STAGE_VERTEX_BIT;
            name = "Vertex + Tessellation Control Shaders";
            description = "Combined Vulkan Vertex and Tessellation Control Shaders";
         } else {
            name = "Tessellation Control Shader";
            description = "Vulkan Tessellation Control Shader";
         }
         break;
      case MESA_SHADER_TESS_EVAL:
         name = "Tessellation Evaluation Shader";
         description = "Vulkan Tessellation Evaluation Shader";
         break;
      case MESA_SHADER_GEOMETRY:
         if (pipeline->shaders[MESA_SHADER_TESS_CTRL] && !pipeline->shaders[MESA_SHADER_TESS_EVAL]) {
            pProperties[executable_idx].stages |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
            name = "Tessellation Evaluation + Geometry Shaders";
            description = "Combined Vulkan Tessellation Evaluation and Geometry Shaders";
         } else if (!pipeline->shaders[MESA_SHADER_TESS_CTRL] && !pipeline->shaders[MESA_SHADER_VERTEX]) {
            pProperties[executable_idx].stages |= VK_SHADER_STAGE_VERTEX_BIT;
            name = "Vertex + Geometry Shader";
            description = "Combined Vulkan Vertex and Geometry Shaders";
         } else {
            name = "Geometry Shader";
            description = "Vulkan Geometry Shader";
         }
         break;
      case MESA_SHADER_FRAGMENT:
         name = "Fragment Shader";
         description = "Vulkan Fragment Shader";
         break;
      case MESA_SHADER_COMPUTE:
         name = "Compute Shader";
         description = "Vulkan Compute Shader";
         break;
      case MESA_SHADER_MESH:
         name = "Mesh Shader";
         description = "Vulkan Mesh Shader";
         break;
      case MESA_SHADER_TASK:
         name = "Task Shader";
         description = "Vulkan Task Shader";
         break;
      case MESA_SHADER_RAYGEN:
         name = "Ray Generation Shader";
         description = "Vulkan Ray Generation Shader";
         break;
      case MESA_SHADER_ANY_HIT:
         name = "Any-Hit Shader";
         description = "Vulkan Any-Hit Shader";
         break;
      case MESA_SHADER_CLOSEST_HIT:
         name = "Closest-Hit Shader";
         description = "Vulkan Closest-Hit Shader";
         break;
      case MESA_SHADER_MISS:
         name = "Miss Shader";
         description = "Vulkan Miss Shader";
         break;
      case MESA_SHADER_INTERSECTION:
         name = "Intersection Shader";
         description = "Vulkan Intersection Shader";
         break;
      case MESA_SHADER_CALLABLE:
         name = "Callable Shader";
         description = "Vulkan Callable Shader";
         break;
      }

      pProperties[executable_idx].subgroupSize = pipeline->shaders[i]->info.wave_size;
      desc_copy(pProperties[executable_idx].name, name);
      desc_copy(pProperties[executable_idx].description, description);

      ++executable_idx;
      if (i == MESA_SHADER_GEOMETRY &&
          !radv_pipeline_has_ngg(radv_pipeline_to_graphics(pipeline))) {
         assert(pipeline->gs_copy_shader);
         if (executable_idx >= count)
            break;

         pProperties[executable_idx].stages = VK_SHADER_STAGE_GEOMETRY_BIT;
         pProperties[executable_idx].subgroupSize = 64;
         desc_copy(pProperties[executable_idx].name, "GS Copy Shader");
         desc_copy(pProperties[executable_idx].description,
                   "Extra shader stage that loads the GS output ringbuffer into the rasterizer");

         ++executable_idx;
      }
   }

   VkResult result = *pExecutableCount < total_count ? VK_INCOMPLETE : VK_SUCCESS;
   *pExecutableCount = count;
   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetPipelineExecutableStatisticsKHR(VkDevice _device,
                                        const VkPipelineExecutableInfoKHR *pExecutableInfo,
                                        uint32_t *pStatisticCount,
                                        VkPipelineExecutableStatisticKHR *pStatistics)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_pipeline, pipeline, pExecutableInfo->pipeline);
   gl_shader_stage stage;
   struct radv_shader *shader =
      radv_get_shader_from_executable_index(pipeline, pExecutableInfo->executableIndex, &stage);

   const struct radv_physical_device *pdevice = device->physical_device;

   unsigned lds_increment = pdevice->rad_info.gfx_level >= GFX11 && stage == MESA_SHADER_FRAGMENT
      ? 1024 : pdevice->rad_info.lds_encode_granularity;
   unsigned max_waves = radv_get_max_waves(device, shader, stage);

   VkPipelineExecutableStatisticKHR *s = pStatistics;
   VkPipelineExecutableStatisticKHR *end = s + (pStatistics ? *pStatisticCount : 0);
   VkResult result = VK_SUCCESS;

   if (s < end) {
      desc_copy(s->name, "Driver pipeline hash");
      desc_copy(s->description, "Driver pipeline hash used by RGP");
      s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      s->value.u64 = pipeline->pipeline_hash;
   }
   ++s;

   if (s < end) {
      desc_copy(s->name, "SGPRs");
      desc_copy(s->description, "Number of SGPR registers allocated per subgroup");
      s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      s->value.u64 = shader->config.num_sgprs;
   }
   ++s;

   if (s < end) {
      desc_copy(s->name, "VGPRs");
      desc_copy(s->description, "Number of VGPR registers allocated per subgroup");
      s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      s->value.u64 = shader->config.num_vgprs;
   }
   ++s;

   if (s < end) {
      desc_copy(s->name, "Spilled SGPRs");
      desc_copy(s->description, "Number of SGPR registers spilled per subgroup");
      s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      s->value.u64 = shader->config.spilled_sgprs;
   }
   ++s;

   if (s < end) {
      desc_copy(s->name, "Spilled VGPRs");
      desc_copy(s->description, "Number of VGPR registers spilled per subgroup");
      s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      s->value.u64 = shader->config.spilled_vgprs;
   }
   ++s;

   if (s < end) {
      desc_copy(s->name, "Code size");
      desc_copy(s->description, "Code size in bytes");
      s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      s->value.u64 = shader->exec_size;
   }
   ++s;

   if (s < end) {
      desc_copy(s->name, "LDS size");
      desc_copy(s->description, "LDS size in bytes per workgroup");
      s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      s->value.u64 = shader->config.lds_size * lds_increment;
   }
   ++s;

   if (s < end) {
      desc_copy(s->name, "Scratch size");
      desc_copy(s->description, "Private memory in bytes per subgroup");
      s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      s->value.u64 = shader->config.scratch_bytes_per_wave;
   }
   ++s;

   if (s < end) {
      desc_copy(s->name, "Subgroups per SIMD");
      desc_copy(s->description, "The maximum number of subgroups in flight on a SIMD unit");
      s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      s->value.u64 = max_waves;
   }
   ++s;

   if (shader->statistics) {
      for (unsigned i = 0; i < aco_num_statistics; i++) {
         const struct aco_compiler_statistic_info *info = &aco_statistic_infos[i];
         if (s < end) {
            desc_copy(s->name, info->name);
            desc_copy(s->description, info->desc);
            s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
            s->value.u64 = shader->statistics[i];
         }
         ++s;
      }
   }

   if (!pStatistics)
      *pStatisticCount = s - pStatistics;
   else if (s > end) {
      *pStatisticCount = end - pStatistics;
      result = VK_INCOMPLETE;
   } else {
      *pStatisticCount = s - pStatistics;
   }

   return result;
}

static VkResult
radv_copy_representation(void *data, size_t *data_size, const char *src)
{
   size_t total_size = strlen(src) + 1;

   if (!data) {
      *data_size = total_size;
      return VK_SUCCESS;
   }

   size_t size = MIN2(total_size, *data_size);

   memcpy(data, src, size);
   if (size)
      *((char *)data + size - 1) = 0;
   return size < total_size ? VK_INCOMPLETE : VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetPipelineExecutableInternalRepresentationsKHR(
   VkDevice _device, const VkPipelineExecutableInfoKHR *pExecutableInfo,
   uint32_t *pInternalRepresentationCount,
   VkPipelineExecutableInternalRepresentationKHR *pInternalRepresentations)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_pipeline, pipeline, pExecutableInfo->pipeline);
   gl_shader_stage stage;
   struct radv_shader *shader =
      radv_get_shader_from_executable_index(pipeline, pExecutableInfo->executableIndex, &stage);

   VkPipelineExecutableInternalRepresentationKHR *p = pInternalRepresentations;
   VkPipelineExecutableInternalRepresentationKHR *end =
      p + (pInternalRepresentations ? *pInternalRepresentationCount : 0);
   VkResult result = VK_SUCCESS;
   /* optimized NIR */
   if (p < end) {
      p->isText = true;
      desc_copy(p->name, "NIR Shader(s)");
      desc_copy(p->description, "The optimized NIR shader(s)");
      if (radv_copy_representation(p->pData, &p->dataSize, shader->nir_string) != VK_SUCCESS)
         result = VK_INCOMPLETE;
   }
   ++p;

   /* backend IR */
   if (p < end) {
      p->isText = true;
      if (radv_use_llvm_for_stage(device, stage)) {
         desc_copy(p->name, "LLVM IR");
         desc_copy(p->description, "The LLVM IR after some optimizations");
      } else {
         desc_copy(p->name, "ACO IR");
         desc_copy(p->description, "The ACO IR after some optimizations");
      }
      if (radv_copy_representation(p->pData, &p->dataSize, shader->ir_string) != VK_SUCCESS)
         result = VK_INCOMPLETE;
   }
   ++p;

   /* Disassembler */
   if (p < end && shader->disasm_string) {
      p->isText = true;
      desc_copy(p->name, "Assembly");
      desc_copy(p->description, "Final Assembly");
      if (radv_copy_representation(p->pData, &p->dataSize, shader->disasm_string) != VK_SUCCESS)
         result = VK_INCOMPLETE;
   }
   ++p;

   if (!pInternalRepresentations)
      *pInternalRepresentationCount = p - pInternalRepresentations;
   else if (p > end) {
      result = VK_INCOMPLETE;
      *pInternalRepresentationCount = end - pInternalRepresentations;
   } else {
      *pInternalRepresentationCount = p - pInternalRepresentations;
   }

   return result;
}

static void
vk_shader_module_finish(void *_module)
{
   struct vk_shader_module *module = _module;
   vk_object_base_finish(&module->base);
}

VkPipelineShaderStageCreateInfo *
radv_copy_shader_stage_create_info(struct radv_device *device, uint32_t stageCount,
                                   const VkPipelineShaderStageCreateInfo *pStages, void *mem_ctx)
{
   VkPipelineShaderStageCreateInfo *new_stages;

   size_t size = sizeof(VkPipelineShaderStageCreateInfo) * stageCount;
   new_stages = ralloc_size(mem_ctx, size);
   if (!new_stages)
      return NULL;

   memcpy(new_stages, pStages, size);

   for (uint32_t i = 0; i < stageCount; i++) {
      RADV_FROM_HANDLE(vk_shader_module, module, new_stages[i].module);

      const VkShaderModuleCreateInfo *minfo =
         vk_find_struct_const(pStages[i].pNext, SHADER_MODULE_CREATE_INFO);

      if (module) {
         struct vk_shader_module *new_module =
            ralloc_size(mem_ctx, sizeof(struct vk_shader_module) + module->size);
         if (!new_module)
            return NULL;

         ralloc_set_destructor(new_module, vk_shader_module_finish);
         vk_object_base_init(&device->vk, &new_module->base, VK_OBJECT_TYPE_SHADER_MODULE);

         new_module->nir = NULL;
         memcpy(new_module->sha1, module->sha1, sizeof(module->sha1));
         new_module->size = module->size;
         memcpy(new_module->data, module->data, module->size);

         module = new_module;
      } else if (minfo) {
         module = ralloc_size(mem_ctx, sizeof(struct vk_shader_module) + minfo->codeSize);
         if (!module)
            return NULL;

         vk_shader_module_init(&device->vk, module, minfo);
      }

      if (module) {
         const VkSpecializationInfo *spec = new_stages[i].pSpecializationInfo;
         if (spec) {
            VkSpecializationInfo *new_spec = ralloc(mem_ctx, VkSpecializationInfo);
            if (!new_spec)
               return NULL;

            new_spec->mapEntryCount = spec->mapEntryCount;
            uint32_t map_entries_size = sizeof(VkSpecializationMapEntry) * spec->mapEntryCount;
            new_spec->pMapEntries = ralloc_size(mem_ctx, map_entries_size);
            if (!new_spec->pMapEntries)
               return NULL;
            memcpy((void *)new_spec->pMapEntries, spec->pMapEntries, map_entries_size);

            new_spec->dataSize = spec->dataSize;
            new_spec->pData = ralloc_size(mem_ctx, spec->dataSize);
            if (!new_spec->pData)
               return NULL;
            memcpy((void *)new_spec->pData, spec->pData, spec->dataSize);

            new_stages[i].pSpecializationInfo = new_spec;
         }

         new_stages[i].module = vk_shader_module_to_handle(module);
         new_stages[i].pName = ralloc_strdup(mem_ctx, new_stages[i].pName);
         if (!new_stages[i].pName)
            return NULL;
         new_stages[i].pNext = NULL;
      }
   }

   return new_stages;
}
