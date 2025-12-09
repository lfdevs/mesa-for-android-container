/*
 * Copyright © 2019 Intel Corporation
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

#include "anv_nir.h"
#include "nir_builder.h"
#include "compiler/brw/brw_nir.h"
#include "util/mesa-sha1.h"

bool
anv_nir_compute_push_layout(nir_shader *nir,
                            const struct anv_physical_device *pdevice,
                            enum brw_robustness_flags robust_flags,
                            const struct anv_nir_push_layout_info *push_info,
                            struct brw_base_prog_key *prog_key,
                            struct brw_stage_prog_data *prog_data,
                            struct anv_pipeline_bind_map *map,
                            const struct anv_pipeline_push_map *push_map,
                            void *mem_ctx)
{
   const struct brw_compiler *compiler = pdevice->compiler;
   const struct intel_device_info *devinfo = compiler->devinfo;
   memset(map->push_ranges, 0, sizeof(map->push_ranges));

   bool has_const_ubo = false;
   unsigned push_start = UINT_MAX, push_end = 0;
   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            switch (intrin->intrinsic) {
            case nir_intrinsic_load_ubo:
               if (brw_nir_ubo_surface_index_is_pushable(intrin->src[0]) &&
                   nir_src_is_const(intrin->src[1]))
                  has_const_ubo = true;
               break;

            case nir_intrinsic_load_uniform:
            case nir_intrinsic_load_push_constant: {
               unsigned base = nir_intrinsic_base(intrin);
               unsigned range = nir_intrinsic_range(intrin);
               push_start = MIN2(push_start, base);
               push_end = MAX2(push_end, base + range);
               /* We need to retain this information to update the push
                * constant on vkCmdDispatch*().
                */
               if (nir->info.stage == MESA_SHADER_COMPUTE &&
                   base >= anv_drv_const_offset(cs.num_work_groups[0]) &&
                   base < (anv_drv_const_offset(cs.num_work_groups[2]) + 4))
                  map->binding_mask |= ANV_PIPELINE_BIND_MASK_USES_NUM_WORKGROUP;
               break;
            }

            default:
               break;
            }
         }
      }
   }

   const bool has_push_intrinsic = push_start <= push_end;

   const bool push_ubo_ranges =
      has_const_ubo && nir->info.stage != MESA_SHADER_COMPUTE &&
      !brw_shader_stage_requires_bindless_resources(nir->info.stage);

   const bool needs_wa_18019110168 =
      nir->info.stage == MESA_SHADER_FRAGMENT &&
      brw_nir_fragment_shader_needs_wa_18019110168(
         devinfo, push_info->mesh_dynamic ? INTEL_SOMETIMES : INTEL_NEVER, nir);

   if (push_ubo_ranges && (robust_flags & BRW_ROBUSTNESS_UBO)) {
      /* We can't on-the-fly adjust our push ranges because doing so would
       * mess up the layout in the shader.  When robustBufferAccess is
       * enabled, we push a mask into the shader indicating which pushed
       * registers are valid and we zero out the invalid ones at the top of
       * the shader.
       */
      const uint32_t push_reg_mask_start =
         anv_drv_const_offset(gfx.push_reg_mask[nir->info.stage]);
      const uint32_t push_reg_mask_end =
         push_reg_mask_start +
         anv_drv_const_size(gfx.push_reg_mask[nir->info.stage]);
      push_start = MIN2(push_start, push_reg_mask_start);
      push_end = MAX2(push_end, push_reg_mask_end);
   }

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      if (push_info->fragment_dynamic) {
         const uint32_t fs_msaa_flags_start =
            anv_drv_const_offset(gfx.fs_msaa_flags);
         const uint32_t fs_msaa_flags_end =
            fs_msaa_flags_start +
            anv_drv_const_size(gfx.fs_msaa_flags);
         push_start = MIN2(push_start, fs_msaa_flags_start);
         push_end = MAX2(push_end, fs_msaa_flags_end);
      }

      if (needs_wa_18019110168) {
         const uint32_t fs_per_prim_remap_start =
            anv_drv_const_offset(gfx.fs_per_prim_remap_offset);
         const uint32_t fs_per_prim_remap_end =
            fs_per_prim_remap_start +
            anv_drv_const_size(gfx.fs_per_prim_remap_offset);
         push_start = MIN2(push_start, fs_per_prim_remap_start);
         push_end = MAX2(push_end, fs_per_prim_remap_end);
      }
   }

   const bool needs_dyn_tess_config =
      (nir->info.stage == MESA_SHADER_TESS_CTRL &&
       (container_of(prog_key, struct brw_tcs_prog_key, base)->input_vertices == 0 ||
        push_info->separate_tessellation)) ||
      (nir->info.stage == MESA_SHADER_TESS_EVAL &&
       push_info->separate_tessellation);
   if (needs_dyn_tess_config) {
      const uint32_t tess_config_start = anv_drv_const_offset(gfx.tess_config);
      const uint32_t tess_config_end = tess_config_start +
                                       anv_drv_const_size(gfx.tess_config);
      push_start = MIN2(push_start, tess_config_start);
      push_end = MAX2(push_end, tess_config_end);
   }

   if (nir->info.stage == MESA_SHADER_COMPUTE && devinfo->verx10 < 125) {
      /* For compute shaders, we always have to have the subgroup ID.  The
       * back-end compiler will "helpfully" add it for us in the last push
       * constant slot.  Yes, there is an off-by-one error here but that's
       * because the back-end will add it so we want to claim the number of
       * push constants one dword less than the full amount including
       * gl_SubgroupId.
       */
      assert(push_end <= anv_drv_const_offset(cs.subgroup_id));
      push_end = anv_drv_const_offset(cs.subgroup_id);
   }

   /* Align push_start down to a 32B (for 3DSTATE_CONSTANT) and make it no
    * larger than push_end (no push constants is indicated by push_start =
    * UINT_MAX).
    *
    * If we were to use
    * 3DSTATE_(MESH|TASK)_SHADER_DATA::IndirectDataStartAddress we would need
    * to align things to 64B.
    *
    * SKL PRMs, Volume 2d: Command Reference: Structures,
    * 3DSTATE_CONSTANT::Constant Buffer 0 Read Length:
    *
    *    "This field specifies the length of the constant data to be loaded
    *     from memory in 256-bit units."
    *
    * ATS-M PRMs, Volume 2d: Command Reference: Structures,
    * 3DSTATE_MESH_SHADER_DATA_BODY::Indirect Data Start Address:
    *
    *    "This pointer is relative to the General State Base Address. It is
    *     the 64-byte aligned address of the indirect data."
    *
    * COMPUTE_WALKER::Indirect Data Start Address has the same requirements as
    * 3DSTATE_MESH_SHADER_DATA_BODY::Indirect Data Start Address but the push
    * constant allocation for compute shader is not shared with other stages
    * (unlike all Gfx stages) and so we can bound+align the allocation there
    * (see anv_cmd_buffer_cs_push_constants).
    */
   push_start = MIN2(push_start, push_end);
   push_start = ROUND_DOWN_TO(push_start, 32);

   /* For scalar, push data size needs to be aligned to a DWORD. */
   const unsigned alignment = 4;
   nir->num_uniforms = align(push_end - push_start, alignment);
   prog_data->nr_params = nir->num_uniforms / 4;
   prog_data->param = rzalloc_array(mem_ctx, uint32_t, prog_data->nr_params);

   struct anv_push_range push_constant_range = {
      .set = ANV_DESCRIPTOR_SET_PUSH_CONSTANTS,
      .start = push_start / 32,
      .length = align(push_end - push_start, devinfo->grf_size) / 32,
   };

   if (has_push_intrinsic) {
      nir_foreach_function_impl(impl, nir) {
         nir_foreach_block(block, impl) {
            nir_foreach_instr_safe(instr, block) {
               if (instr->type != nir_instr_type_intrinsic)
                  continue;

               nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
               switch (intrin->intrinsic) {
               case nir_intrinsic_load_uniform:
               case nir_intrinsic_load_push_constant: {
                  /* With bindless shaders we load uniforms with SEND
                   * messages. All the push constants are located after the
                   * RT_DISPATCH_GLOBALS. We just need to add the offset to
                   * the address right after RT_DISPATCH_GLOBALS (see
                   * brw_nir_lower_rt_intrinsics.c).
                   */
                  unsigned base_offset =
                     brw_shader_stage_is_bindless(nir->info.stage) ? 0 : push_start;
                  nir_intrinsic_set_base(intrin,
                                         nir_intrinsic_base(intrin) -
                                         base_offset);
                  break;
               }

               default:
                  break;
               }
            }
         }
      }
   }

   /* When platforms support Mesh and the fragment shader is not fully linked
    * to the previous shader, payload format can change if the preceding
    * shader is mesh or not, this is an issue in particular for PrimitiveID
    * value (in legacy it's delivered as a VUE slot, in mesh it's delivered as
    * in the per-primitive block).
    *
    * Here is the difference in payload format :
    *
    *       Legacy                 Mesh
    * -------------------   -------------------
    * |      ...        |   |      ...        |
    * |-----------------|   |-----------------|
    * |  Constant data  |   |  Constant data  |
    * |-----------------|   |-----------------|
    * | VUE attributes  |   | Per Primive data|
    * -------------------   |-----------------|
    *                       | VUE attributes  |
    *                       -------------------
    *
    * To solve that issue we push an additional dummy push constant buffer in
    * legacy pipelines to align everything. The compiler then adds a SEL
    * instruction to source the PrimitiveID from the right location based on a
    * dynamic bit in fs_msaa_intel.
    */
   const bool needs_padding_per_primitive =
      needs_wa_18019110168 ||
      (push_info->mesh_dynamic &&
       (nir->info.inputs_read & VARYING_BIT_PRIMITIVE_ID));

   unsigned n_push_ranges = 0;
   if (push_ubo_ranges) {
      brw_nir_analyze_ubo_ranges(compiler, nir, prog_data->ubo_ranges);

      const unsigned max_push_regs = 64;

      unsigned total_push_regs = push_constant_range.length;
      for (unsigned i = 0; i < 4; i++) {
         if (total_push_regs + prog_data->ubo_ranges[i].length > max_push_regs)
            prog_data->ubo_ranges[i].length = max_push_regs - total_push_regs;
         total_push_regs += prog_data->ubo_ranges[i].length;
      }
      assert(total_push_regs <= max_push_regs);

      if (push_constant_range.length > 0)
         map->push_ranges[n_push_ranges++] = push_constant_range;

      if (robust_flags & BRW_ROBUSTNESS_UBO) {
         const uint32_t push_reg_mask_offset =
            anv_drv_const_offset(gfx.push_reg_mask[nir->info.stage]);
         assert(push_reg_mask_offset >= push_start);
         prog_data->push_reg_mask_param =
            (push_reg_mask_offset - push_start) / 4;
      }

      const unsigned max_push_buffers = needs_padding_per_primitive ? 3 : 4;

      for (unsigned i = 0; i < 4; i++) {
         struct brw_ubo_range *ubo_range = &prog_data->ubo_ranges[i];
         if (ubo_range->length == 0)
            continue;

         if (n_push_ranges >= max_push_buffers) {
            memset(ubo_range, 0, sizeof(*ubo_range));
            continue;
         }

         assert(ubo_range->block < push_map->block_count);
         const struct anv_pipeline_binding *binding =
            &push_map->block_to_descriptor[ubo_range->block];

         map->push_ranges[n_push_ranges++] = (struct anv_push_range) {
            .set = binding->set,
            .index = binding->index,
            .dynamic_offset_index = binding->dynamic_offset_index,
            .start = ubo_range->start,
            .length = ubo_range->length,
         };

         /* We only bother to shader-zero pushed client UBOs */
         if (binding->set < MAX_SETS &&
             (robust_flags & BRW_ROBUSTNESS_UBO)) {
            prog_data->robust_ubo_ranges |= (uint8_t) (1 << i);
         }
      }
   } else if (push_constant_range.length > 0) {
      /* For Ivy Bridge, the push constants packets have a different
       * rule that would require us to iterate in the other direction
       * and possibly mess around with dynamic state base address.
       * Don't bother; just emit regular push constants at n = 0.
       *
       * In the compute case, we don't have multiple push ranges so it's
       * better to just provide one in push_ranges[0].
       */
      map->push_ranges[n_push_ranges++] = push_constant_range;
   }

   /* Pass a single-register push constant payload for the PS stage even if
    * empty, since PS invocations with zero push constant cycles have been
    * found to cause hangs with TBIMR enabled. See HSDES #22020184996.
    *
    * XXX - Use workaround infrastructure and final workaround when provided
    *       by hardware team.
    */
   if (n_push_ranges == 0 &&
       nir->info.stage == MESA_SHADER_FRAGMENT &&
       devinfo->needs_null_push_constant_tbimr_workaround) {
      map->push_ranges[n_push_ranges++] = (struct anv_push_range) {
         .set = ANV_DESCRIPTOR_SET_NULL,
         .start = 0,
         .length = 1,
      };
      assert(prog_data->nr_params == 0);
      prog_data->nr_params = 32 / 4;
   }

   if (needs_padding_per_primitive) {
      struct anv_push_range push_constant_range = {
         .set = ANV_DESCRIPTOR_SET_PER_PRIM_PADDING,
         .start = 0,
         .length = 1,
      };
      map->push_ranges[n_push_ranges++] = push_constant_range;
   }

   assert(n_push_ranges <= 4);

   if (nir->info.stage == MESA_SHADER_TESS_CTRL && needs_dyn_tess_config) {
      struct brw_tcs_prog_data *tcs_prog_data = brw_tcs_prog_data(prog_data);

      const uint32_t tess_config_offset = anv_drv_const_offset(gfx.tess_config);
      assert(tess_config_offset >= push_start);
      tcs_prog_data->tess_config_param = (tess_config_offset - push_start) / 4;
   }
   if (nir->info.stage == MESA_SHADER_TESS_EVAL && push_info->separate_tessellation) {
      struct brw_tes_prog_data *tes_prog_data = brw_tes_prog_data(prog_data);

      const uint32_t tess_config_offset = anv_drv_const_offset(gfx.tess_config);
      assert(tess_config_offset >= push_start);
      tes_prog_data->tess_config_param = (tess_config_offset - push_start) / 4;
   }
   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      struct brw_wm_prog_data *wm_prog_data =
         container_of(prog_data, struct brw_wm_prog_data, base);

      if (push_info->fragment_dynamic) {
         const uint32_t fs_msaa_flags_offset =
            anv_drv_const_offset(gfx.fs_msaa_flags);
         assert(fs_msaa_flags_offset >= push_start);
         wm_prog_data->msaa_flags_param =
            (fs_msaa_flags_offset - push_start) / 4;
      }

      if (needs_wa_18019110168) {
         const uint32_t fs_per_prim_remap_offset =
            anv_drv_const_offset(gfx.fs_per_prim_remap_offset);
         assert(fs_per_prim_remap_offset >= push_start);
         wm_prog_data->per_primitive_remap_param =
            (fs_per_prim_remap_offset - push_start) / 4;
      }
   }

#if 0
   fprintf(stderr, "stage=%s push ranges:\n", mesa_shader_stage_name(nir->info.stage));
   for (unsigned i = 0; i < ARRAY_SIZE(map->push_ranges); i++)
      fprintf(stderr, "   range%i: %03u-%03u set=%u index=%u\n", i,
              map->push_ranges[i].start,
              map->push_ranges[i].length,
              map->push_ranges[i].set,
              map->push_ranges[i].index);
#endif

   /* Now that we're done computing the push constant portion of the
    * bind map, hash it.  This lets us quickly determine if the actual
    * mapping has changed and not just a no-op pipeline change.
    */
   _mesa_sha1_compute(map->push_ranges,
                      sizeof(map->push_ranges),
                      map->push_sha1);
   return false;
}

void
anv_nir_validate_push_layout(const struct anv_physical_device *pdevice,
                             struct brw_stage_prog_data *prog_data,
                             struct anv_pipeline_bind_map *map)
{
#ifndef NDEBUG
   unsigned prog_data_push_size = align(prog_data->nr_params, pdevice->info.grf_size / 4) / 8;

   for (unsigned i = 0; i < 4; i++)
      prog_data_push_size += prog_data->ubo_ranges[i].length;

   unsigned bind_map_push_size = 0;
   for (unsigned i = 0; i < 4; i++) {
      /* This is dynamic and doesn't count against prog_data->ubo_ranges[] */
      if (map->push_ranges[i].set == ANV_DESCRIPTOR_SET_PER_PRIM_PADDING)
         continue;
      bind_map_push_size += map->push_ranges[i].length;
   }

   /* We could go through everything again but it should be enough to assert
    * that they push the same number of registers.  This should alert us if
    * the back-end compiler decides to re-arrange stuff or shrink a range.
    */
   assert(prog_data_push_size == bind_map_push_size);
#endif
}
