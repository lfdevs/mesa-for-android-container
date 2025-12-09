/* Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 */

#include "si_shader_internal.h"
#include "si_pipe.h"

static void declare_streamout_params(struct si_shader_args *args, struct si_shader *shader,
                                     const shader_info *info)
{
   if (shader->selector->screen->info.gfx_level >= GFX11) {
      /* NGG streamout. */
      if (info->stage == MESA_SHADER_TESS_EVAL)
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL);
      return;
   }

   /* Streamout SGPRs. */
   if (shader->info.num_streamout_vec4s) {
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.streamout_config);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.streamout_write_index);

      /* A streamout buffer offset is loaded if the stride is non-zero. */
      for (int i = 0; i < 4; i++) {
         if (!info->xfb_stride[i])
            continue;

         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.streamout_offset[i]);
      }
   } else if (info->stage == MESA_SHADER_TESS_EVAL) {
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL);
   }
}

static void declare_const_and_shader_buffers(struct si_shader_args *args, struct si_shader *shader,
                                             const shader_info *info, bool assign_params)
{
   enum ac_arg_type const_shader_buf_type;

   if (info->num_ubos == 1 && info->num_ssbos == 0)
      const_shader_buf_type = AC_ARG_CONST_ADDR;
   else
      const_shader_buf_type = AC_ARG_CONST_ADDR;

   ac_add_arg(
      &args->ac, AC_ARG_SGPR, 1, const_shader_buf_type,
      assign_params ? &args->const_and_shader_buffers : &args->other_const_and_shader_buffers);
}

static void declare_samplers_and_images(struct si_shader_args *args, bool assign_params)
{
   ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_CONST_ADDR,
              assign_params ? &args->samplers_and_images : &args->other_samplers_and_images);
}

static void declare_per_stage_desc_pointers(struct si_shader_args *args, struct si_shader *shader,
                                            const shader_info *info, bool assign_params)
{
   declare_const_and_shader_buffers(args, shader, info, assign_params);
   declare_samplers_and_images(args, assign_params);
}

static void declare_global_desc_pointers(struct si_shader_args *args)
{
   ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_CONST_ADDR, &args->internal_bindings);
   ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_CONST_ADDR,
              &args->bindless_samplers_and_images);
}

static void declare_vb_descriptor_input_sgprs(struct si_shader_args *args,
                                              struct si_shader *shader)
{
   ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_CONST_ADDR, &args->ac.vertex_buffers);

   unsigned num_vbos_in_user_sgprs = shader->selector->info.num_vbos_in_user_sgprs;
   if (num_vbos_in_user_sgprs) {
      unsigned user_sgprs = args->ac.num_sgprs_used;

      if (si_is_merged_shader(shader))
         user_sgprs -= 8;
      assert(user_sgprs <= SI_SGPR_VS_VB_DESCRIPTOR_FIRST);

      /* Declare unused SGPRs to align VB descriptors to 4 SGPRs (hw requirement). */
      for (unsigned i = user_sgprs; i < SI_SGPR_VS_VB_DESCRIPTOR_FIRST; i++)
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL); /* unused */

      assert(num_vbos_in_user_sgprs <= ARRAY_SIZE(args->vb_descriptors));
      for (unsigned i = 0; i < num_vbos_in_user_sgprs; i++)
         ac_add_arg(&args->ac, AC_ARG_SGPR, 4, AC_ARG_VALUE, &args->vb_descriptors[i]);
   }
}

static void declare_vs_input_vgprs(struct si_shader_args *args, struct si_shader *shader)
{
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.vertex_id);

   if (shader->selector->screen->info.gfx_level >= GFX12) {
      ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.instance_id);
   } else if (shader->key.ge.as_ls) {
      if (shader->selector->screen->info.gfx_level >= GFX11) {
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, NULL); /* user VGPR */
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, NULL); /* user VGPR */
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.instance_id);
      } else if (shader->selector->screen->info.gfx_level >= GFX10) {
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.vs_rel_patch_id);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, NULL); /* user VGPR */
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.instance_id);
      } else {
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.vs_rel_patch_id);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.instance_id);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, NULL); /* unused */
      }
   } else if (shader->selector->screen->info.gfx_level >= GFX10) {
      ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, NULL); /* user VGPR */
      ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE,
                 /* user vgpr or PrimID (legacy) */
                 shader->key.ge.as_ngg ? NULL : &args->ac.vs_prim_id);
      ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.instance_id);
   } else {
      ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.instance_id);
      ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.vs_prim_id);
      ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, NULL); /* unused */
   }
}

static void declare_vs_blit_inputs(struct si_shader *shader, struct si_shader_args *args,
                                   const shader_info *info)
{
   bool has_attribute_ring_address = shader->selector->screen->info.gfx_level >= GFX11;

   ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->vs_blit_inputs); /* i16 x1, y1 */
   ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL);                  /* i16 x1, y1 */
   ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL);                /* depth */

   if (info->vs.blit_sgprs_amd ==
       SI_VS_BLIT_SGPRS_POS_TEXCOORD + has_attribute_ring_address) {
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL); /* texcoord.x1 */
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL); /* texcoord.y1 */
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL); /* texcoord.x2 */
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL); /* texcoord.y2 */
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL); /* texcoord.z */
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL); /* texcoord.w */
      if (has_attribute_ring_address)
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL); /* attribute ring address */
   }
}

static void declare_tes_input_vgprs(struct si_shader_args *args)
{
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.tes_u);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.tes_v);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.tes_rel_patch_id);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.tes_patch_id);
}

enum {
   /* Convenient merged shader definitions. */
   SI_SHADER_MERGED_VERTEX_TESSCTRL = MESA_ALL_SHADER_STAGES,
   SI_SHADER_MERGED_VERTEX_OR_TESSEVAL_GEOMETRY,
};

static void si_add_arg_checked(struct ac_shader_args *args, enum ac_arg_regfile file, unsigned registers,
                               enum ac_arg_type type, struct ac_arg *arg, unsigned idx)
{
   assert(args->arg_count == idx);
   ac_add_arg(args, file, registers, type, arg);
}

void si_init_shader_args(struct si_shader *shader, struct si_shader_args *args,
                         const shader_info *info)
{
   unsigned i, num_returns, num_return_sgprs;
   unsigned num_prolog_vgprs = 0;
   struct si_shader_selector *sel = shader->selector;
   unsigned stage = shader->is_gs_copy_shader ? MESA_SHADER_VERTEX : info->stage;
   unsigned stage_case = stage;

   memset(args, 0, sizeof(*args));

   /* Set MERGED shaders. */
   if (sel->screen->info.gfx_level >= GFX9 && stage <= MESA_SHADER_GEOMETRY) {
      if (shader->key.ge.as_ls || stage == MESA_SHADER_TESS_CTRL)
         stage_case = SI_SHADER_MERGED_VERTEX_TESSCTRL; /* LS or HS */
      else if (shader->key.ge.as_es || shader->key.ge.as_ngg || stage == MESA_SHADER_GEOMETRY)
         stage_case = SI_SHADER_MERGED_VERTEX_OR_TESSEVAL_GEOMETRY;
   }

   switch (stage_case) {
   case MESA_SHADER_VERTEX:
      declare_global_desc_pointers(args);

      if (info->vs.blit_sgprs_amd) {
         declare_vs_blit_inputs(shader, args, info);
      } else {
         declare_per_stage_desc_pointers(args, shader, info, true);
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->vs_state_bits);

         if (shader->is_gs_copy_shader) {
            declare_streamout_params(args, shader, info);
         } else {
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.base_vertex);
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.draw_id);
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.start_instance);
            declare_vb_descriptor_input_sgprs(args, shader);

            if (shader->key.ge.as_es) {
               ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.es2gs_offset);
            } else if (shader->key.ge.as_ls) {
               /* no extra parameters */
            } else {
               declare_streamout_params(args, shader, info);
            }
         }
      }

      /* GFX11 set FLAT_SCRATCH directly instead of using this arg. */
      if (info->use_aco_amd && sel->screen->info.gfx_level < GFX11)
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.scratch_offset);

      /* VGPRs */
      declare_vs_input_vgprs(args, shader);

      break;

   case MESA_SHADER_TESS_CTRL: /* GFX6-GFX8 */
      declare_global_desc_pointers(args);
      declare_per_stage_desc_pointers(args, shader, info, true);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.tcs_offchip_layout);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->tes_offchip_addr);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->vs_state_bits);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.tess_offchip_offset);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.tcs_factor_offset);

      /* GFX11 set FLAT_SCRATCH directly instead of using this arg. */
      if (info->use_aco_amd && sel->screen->info.gfx_level < GFX11)
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.scratch_offset);

      /* VGPRs */
      ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.tcs_patch_id);
      ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.tcs_rel_ids);
      break;

   case SI_SHADER_MERGED_VERTEX_TESSCTRL:
      /* Merged stages have 8 system SGPRs at the beginning. */
      /* Gfx9-10: SPI_SHADER_USER_DATA_ADDR_LO/HI_HS */
      /* Gfx11+:  SPI_SHADER_PGM_LO/HI_HS */
      declare_per_stage_desc_pointers(args, shader, info, stage == MESA_SHADER_TESS_CTRL);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.tess_offchip_offset);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.merged_wave_info);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.tcs_factor_offset);
      if (sel->screen->info.gfx_level >= GFX11)
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.tcs_wave_id);
      else
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.scratch_offset);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL); /* unused */
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL); /* unused */

      declare_global_desc_pointers(args);
      declare_per_stage_desc_pointers(args, shader, info, stage == MESA_SHADER_VERTEX);

      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->vs_state_bits);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.base_vertex);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.draw_id);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.start_instance);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.tcs_offchip_layout);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->tes_offchip_addr);

      /* VGPRs (first TCS, then VS) */
      ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.tcs_patch_id);
      ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.tcs_rel_ids);

      if (stage == MESA_SHADER_VERTEX) {
         declare_vs_input_vgprs(args, shader);

         /* Need to keep LS/HS arg index same for shared args when ACO,
          * so this is not able to be before shared VGPRs.
          */
         declare_vb_descriptor_input_sgprs(args, shader);

         /* LS return values are inputs to the TCS main shader part. */
         if (!shader->is_monolithic || shader->key.ge.opt.same_patch_vertices) {
            for (i = 0; i < 8 + GFX9_TCS_NUM_USER_SGPR; i++)
               ac_add_return(&args->ac, AC_ARG_SGPR);
            for (i = 0; i < 2; i++)
               ac_add_return(&args->ac, AC_ARG_VGPR);

            /* VS outputs passed via VGPRs to TCS. */
            if (shader->key.ge.opt.same_patch_vertices && !info->use_aco_amd) {
               unsigned num_outputs = util_last_bit64(shader->selector->info.ls_es_outputs_written);
               for (i = 0; i < num_outputs * 4; i++)
                  ac_add_return(&args->ac, AC_ARG_VGPR);
            }
         }
      } else {
         /* TCS inputs are passed via VGPRs from VS. */
         if (shader->key.ge.opt.same_patch_vertices && !info->use_aco_amd) {
            unsigned num_inputs = util_last_bit64(shader->previous_stage_sel->info.ls_es_outputs_written);
            for (i = 0; i < num_inputs * 4; i++)
               ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, NULL);
         }
      }
      break;

   case SI_SHADER_MERGED_VERTEX_OR_TESSEVAL_GEOMETRY:
      /* Merged stages have 8 system SGPRs at the beginning. */
      /* Gfx9-10: SPI_SHADER_USER_DATA_ADDR_LO/HI_GS */
      /* Gfx11+:  SPI_SHADER_PGM_LO/HI_GS */
      declare_per_stage_desc_pointers(args, shader, info, stage == MESA_SHADER_GEOMETRY);

      if (shader->key.ge.as_ngg)
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.gs_tg_info);
      else
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.gs2vs_offset);

      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.merged_wave_info);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.tess_offchip_offset);
      if (sel->screen->info.gfx_level >= GFX11)
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.gs_attr_offset);
      else
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.scratch_offset);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL); /* unused */
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL); /* unused */

      declare_global_desc_pointers(args);
      if (stage != MESA_SHADER_VERTEX || !info->vs.blit_sgprs_amd) {
         declare_per_stage_desc_pointers(
            args, shader, info, (stage == MESA_SHADER_VERTEX || stage == MESA_SHADER_TESS_EVAL));
      }

      if (stage == MESA_SHADER_VERTEX && info->vs.blit_sgprs_amd) {
         declare_vs_blit_inputs(shader, args, info);
      } else {
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->vs_state_bits);

         if (stage == MESA_SHADER_VERTEX) {
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.base_vertex);
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.draw_id);
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.start_instance);
         } else if (stage == MESA_SHADER_TESS_EVAL) {
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.tcs_offchip_layout);
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->tes_offchip_addr);
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL); /* unused */
         } else {
            /* GS */
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL); /* unused */
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL); /* unused */
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL); /* unused */
         }

         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_CONST_ADDR, &args->small_prim_cull_info);
         if (sel->screen->info.gfx_level >= GFX11)
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->gs_attr_address);
         else
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL); /* unused */
      }

      /* VGPRs (first GS, then VS/TES) */
      if (sel->screen->info.gfx_level >= GFX12) {
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.gs_vtx_offset[0]);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.gs_prim_id);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.gs_vtx_offset[1]);
      } else {
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.gs_vtx_offset[0]);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.gs_vtx_offset[1]);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.gs_prim_id);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.gs_invocation_id);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.gs_vtx_offset[2]);
      }

      if (stage == MESA_SHADER_VERTEX) {
         declare_vs_input_vgprs(args, shader);

         /* Need to keep ES/GS arg index same for shared args when ACO,
          * so this is not able to be before shared VGPRs.
          */
         if (!info->vs.blit_sgprs_amd)
            declare_vb_descriptor_input_sgprs(args, shader);
      } else if (stage == MESA_SHADER_TESS_EVAL) {
         declare_tes_input_vgprs(args);
      }

      if (shader->key.ge.as_es && !shader->is_monolithic &&
          (stage == MESA_SHADER_VERTEX || stage == MESA_SHADER_TESS_EVAL)) {
         /* ES return values are inputs to GS. */
         for (i = 0; i < 8 + GFX9_GS_NUM_USER_SGPR; i++)
            ac_add_return(&args->ac, AC_ARG_SGPR);
         for (i = 0; i < (sel->screen->info.gfx_level >= GFX12 ? 3 : 5); i++)
            ac_add_return(&args->ac, AC_ARG_VGPR);
      }
      break;

   case MESA_SHADER_TESS_EVAL:
      declare_global_desc_pointers(args);
      declare_per_stage_desc_pointers(args, shader, info, true);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->vs_state_bits);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.tcs_offchip_layout);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->tes_offchip_addr);

      if (shader->key.ge.as_es) {
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.tess_offchip_offset);
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL);
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.es2gs_offset);
      } else {
         declare_streamout_params(args, shader, info);
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.tess_offchip_offset);
      }

      /* GFX11 set FLAT_SCRATCH directly instead of using this arg. */
      if (info->use_aco_amd && sel->screen->info.gfx_level < GFX11)
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.scratch_offset);

      /* VGPRs */
      declare_tes_input_vgprs(args);
      break;

   case MESA_SHADER_GEOMETRY:
      declare_global_desc_pointers(args);
      declare_per_stage_desc_pointers(args, shader, info, true);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.gs2vs_offset);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.gs_wave_id);

      /* GFX11 set FLAT_SCRATCH directly instead of using this arg. */
      if (info->use_aco_amd && sel->screen->info.gfx_level < GFX11)
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.scratch_offset);

      /* VGPRs */
      ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.gs_vtx_offset[0]);
      ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.gs_vtx_offset[1]);
      ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.gs_prim_id);
      ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.gs_vtx_offset[2]);
      ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.gs_vtx_offset[3]);
      ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.gs_vtx_offset[4]);
      ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.gs_vtx_offset[5]);
      ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.gs_invocation_id);
      break;

   case MESA_SHADER_FRAGMENT:
      declare_global_desc_pointers(args);
      declare_per_stage_desc_pointers(args, shader, info, true);
      si_add_arg_checked(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->sample_locs[0],
                         SI_PARAM_SAMPLE_LOCS0);
      si_add_arg_checked(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->sample_locs[1],
                         SI_PARAM_SAMPLE_LOCS1);
      si_add_arg_checked(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->alpha_reference,
                         SI_PARAM_ALPHA_REF);
      si_add_arg_checked(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.prim_mask,
                         SI_PARAM_PRIM_MASK);

      si_add_arg_checked(&args->ac, AC_ARG_VGPR, 2, AC_ARG_VALUE, &args->ac.persp_sample,
                         SI_PARAM_PERSP_SAMPLE);
      si_add_arg_checked(&args->ac, AC_ARG_VGPR, 2, AC_ARG_VALUE, &args->ac.persp_center,
                         SI_PARAM_PERSP_CENTER);
      si_add_arg_checked(&args->ac, AC_ARG_VGPR, 2, AC_ARG_VALUE, &args->ac.persp_centroid,
                         SI_PARAM_PERSP_CENTROID);
      si_add_arg_checked(&args->ac, AC_ARG_VGPR, 3, AC_ARG_VALUE, NULL, SI_PARAM_PERSP_PULL_MODEL);
      si_add_arg_checked(&args->ac, AC_ARG_VGPR, 2, AC_ARG_VALUE, &args->ac.linear_sample,
                         SI_PARAM_LINEAR_SAMPLE);
      si_add_arg_checked(&args->ac, AC_ARG_VGPR, 2, AC_ARG_VALUE, &args->ac.linear_center,
                         SI_PARAM_LINEAR_CENTER);
      si_add_arg_checked(&args->ac, AC_ARG_VGPR, 2, AC_ARG_VALUE, &args->ac.linear_centroid,
                         SI_PARAM_LINEAR_CENTROID);
      si_add_arg_checked(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, NULL, SI_PARAM_LINE_STIPPLE_TEX);
      si_add_arg_checked(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.frag_pos[0],
                         SI_PARAM_POS_X_FLOAT);
      si_add_arg_checked(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.frag_pos[1],
                         SI_PARAM_POS_Y_FLOAT);
      si_add_arg_checked(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.frag_pos[2],
                         SI_PARAM_POS_Z_FLOAT);
      si_add_arg_checked(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.frag_pos[3],
                         SI_PARAM_POS_W_FLOAT);
      si_add_arg_checked(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.front_face,
                         SI_PARAM_FRONT_FACE);
      si_add_arg_checked(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.ancillary,
                         SI_PARAM_ANCILLARY);
      si_add_arg_checked(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.sample_coverage,
                         SI_PARAM_SAMPLE_COVERAGE);
      si_add_arg_checked(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.pos_fixed_pt,
                         SI_PARAM_POS_FIXED_PT);

      if (info->use_aco_amd) {
         ac_compact_ps_vgpr_args(&args->ac, shader->config.spi_ps_input_addr);

         /* GFX11 set FLAT_SCRATCH directly instead of using this arg. */
         if (sel->screen->info.gfx_level < GFX11)
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.scratch_offset);
      }

      /* Monolithic PS emit prolog and epilog in NIR directly. */
      if (!shader->is_monolithic) {
         /* Color inputs from the prolog. */
         if (shader->selector->info.colors_read) {
            unsigned num_color_elements = util_bitcount(shader->selector->info.colors_read);

            for (i = 0; i < num_color_elements; i++)
               ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, i ? NULL : &args->color_start);

            num_prolog_vgprs += num_color_elements;
         }

         /* Outputs for the epilog. */
         num_return_sgprs = SI_SGPR_ALPHA_REF + 1;
         /* These must always be declared even if Z/stencil/samplemask are killed. */
         num_returns = num_return_sgprs + util_bitcount(shader->selector->info.colors_written) * 4 +
                       sel->info.writes_z + sel->info.writes_stencil + sel->info.writes_samplemask +
                       1 /* SampleMaskIn */;

         for (i = 0; i < num_return_sgprs; i++)
            ac_add_return(&args->ac, AC_ARG_SGPR);
         for (; i < num_returns; i++)
            ac_add_return(&args->ac, AC_ARG_VGPR);
      }
      break;

   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_KERNEL:
   case MESA_SHADER_TASK:
      declare_global_desc_pointers(args);
      declare_per_stage_desc_pointers(args, shader, info, true);
      if (stage == MESA_SHADER_TASK) {
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->task_ring_addr);
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.task_ring_entry);
      }
      if (shader->info.uses_draw_id)
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.draw_id);
      if (shader->selector->info.uses_grid_size)
         ac_add_arg(&args->ac, AC_ARG_SGPR, 3, AC_ARG_VALUE, &args->ac.num_work_groups);
      if (shader->selector->info.uses_variable_block_size)
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->block_size);

      unsigned cs_user_data_dwords =
         info->cs.user_data_components_amd;
      if (cs_user_data_dwords) {
         ac_add_arg(&args->ac, AC_ARG_SGPR, MIN2(cs_user_data_dwords, 4), AC_ARG_VALUE,
                    &args->cs_user_data[0]);
         if (cs_user_data_dwords > 4) {
            ac_add_arg(&args->ac, AC_ARG_SGPR, cs_user_data_dwords - 4, AC_ARG_VALUE,
                       &args->cs_user_data[1]);
         }
      }

      /* Some descriptors can be in user SGPRs. */
      /* Shader buffers in user SGPRs. */
      for (unsigned i = 0; i < shader->selector->cs_num_shaderbufs_in_user_sgprs; i++) {
         while (args->ac.num_sgprs_used % 4 != 0)
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL);

         ac_add_arg(&args->ac, AC_ARG_SGPR, 4, AC_ARG_VALUE, &args->cs_shaderbuf[i]);
      }
      /* Images in user SGPRs. */
      for (unsigned i = 0; i < shader->selector->cs_num_images_in_user_sgprs; i++) {
         unsigned num_sgprs = BITSET_TEST(info->image_buffers, i) ? 4 : 8;

         while (args->ac.num_sgprs_used % num_sgprs != 0)
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL);

         ac_add_arg(&args->ac, AC_ARG_SGPR, num_sgprs, AC_ARG_VALUE, &args->cs_image[i]);
      }

      /* Hardware SGPRs. */
      for (i = 0; i < 3; i++) {
         if (shader->selector->info.uses_block_id[i]) {
            /* GFX12 loads workgroup IDs into ttmp registers, so they are not input SGPRs, but we
             * still need to set this to indicate that they are enabled (for ac_nir_to_llvm).
             */
            if (sel->screen->info.gfx_level >= GFX12)
               args->ac.workgroup_ids[i].used = true;
            else
               ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.workgroup_ids[i]);
         }
      }
      if (shader->selector->info.uses_tg_size)
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.tg_size);

      /* GFX11 set FLAT_SCRATCH directly instead of using this arg. */
      if (info->use_aco_amd && sel->screen->info.gfx_level < GFX11)
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.scratch_offset);

      /* Hardware VGPRs. */
      /* Thread IDs are packed in VGPR0, 10 bits per component or stored in 3 separate VGPRs */
      if (sel->screen->info.gfx_level >= GFX11 ||
          (!sel->screen->info.has_graphics && sel->screen->info.family >= CHIP_MI200)) {
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.local_invocation_ids_packed);
      } else {
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.local_invocation_id_x);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.local_invocation_id_y);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.local_invocation_id_z);
      }
      break;
   case MESA_SHADER_MESH:
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL); /* unused */
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL); /* unused */

      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.gs_tg_info);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.merged_wave_info);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.tess_offchip_offset);

      if (sel->screen->info.gfx_level >= GFX11)
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.gs_attr_offset);
      else
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.scratch_offset);

      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL); /* unused */
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL); /* unused */

      /* User SGPRs */
      declare_global_desc_pointers(args);
      declare_per_stage_desc_pointers(args, shader, info, true);

      if (sel->screen->info.gfx_level >= GFX11)
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->gs_attr_address);

      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.task_ring_entry);

      if (info->task_payload_size)
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->task_ring_addr);

      if (shader->info.uses_draw_id)
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->ac.draw_id);

      if (sel->info.uses_grid_size)
         ac_add_arg(&args->ac, AC_ARG_SGPR, 3, AC_ARG_VALUE, &args->ac.num_work_groups);
      else if (sel->screen->info.gfx_level < GFX11)
         /* GFX10 always write grid size to SGPR, reserve space for it */
         ac_add_arg(&args->ac, AC_ARG_SGPR, 3, AC_ARG_VALUE, NULL);

      /* We don't know at this point whether this arg is needed until ac_lower_ngg_mesh,
       * so add this arg at last to not affect other args.
       */
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->mesh_scratch_ring_addr);

      /* VGPRs */
      if (sel->screen->info.mesh_fast_launch_2) {
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.local_invocation_ids_packed);
      } else {
         unsigned unused_args = sel->screen->info.gfx_level >= GFX12 ? 3 : 5;
         ac_add_arg(&args->ac, AC_ARG_VGPR, unused_args, AC_ARG_VALUE, NULL);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.vertex_id);
      }
      break;
   default:
      assert(0 && "unimplemented shader");
      return;
   }

   shader->info.num_input_sgprs = args->ac.num_sgprs_used;
   shader->info.num_input_vgprs = args->ac.num_vgprs_used;

   assert(shader->info.num_input_vgprs >= num_prolog_vgprs);
   shader->info.num_input_vgprs -= num_prolog_vgprs;
}

void si_get_ps_prolog_args(struct si_shader_args *args,
                           const union si_shader_part_key *key)
{
   memset(args, 0, sizeof(*args));

   const unsigned num_input_sgprs = key->ps_prolog.num_input_sgprs;

   struct ac_arg input_sgprs[num_input_sgprs];
   for (unsigned i = 0; i < num_input_sgprs; i++)
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, input_sgprs + i);

   args->internal_bindings = input_sgprs[SI_SGPR_INTERNAL_BINDINGS];
   /* Use the absolute location of the input. */
   args->ac.prim_mask = input_sgprs[SI_PS_NUM_USER_SGPR];

   ac_add_arg(&args->ac, AC_ARG_VGPR, 2, AC_ARG_VALUE, &args->ac.persp_sample);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 2, AC_ARG_VALUE, &args->ac.persp_center);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 2, AC_ARG_VALUE, &args->ac.persp_centroid);
   /* skip PERSP_PULL_MODEL */
   ac_add_arg(&args->ac, AC_ARG_VGPR, 2, AC_ARG_VALUE, &args->ac.linear_sample);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 2, AC_ARG_VALUE, &args->ac.linear_center);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 2, AC_ARG_VALUE, &args->ac.linear_centroid);
   /* skip LINE_STIPPLE_TEX */

   /* POS_X|Y|Z|W_FLOAT */
   u_foreach_bit(i, key->ps_prolog.fragcoord_usage_mask)
      ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.frag_pos[i]);

   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.front_face);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.ancillary);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.sample_coverage);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, &args->ac.pos_fixed_pt);
}

void si_get_ps_epilog_args(struct si_shader_args *args,
                           const union si_shader_part_key *key,
                           struct ac_arg colors[MAX_DRAW_BUFFERS],
                           struct ac_arg *depth, struct ac_arg *stencil,
                           struct ac_arg *sample_mask)
{
   memset(args, 0, sizeof(*args));

   ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL);
   ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL);
   ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL);
   ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL);
   ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL);
   ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, NULL);
   ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_VALUE, &args->alpha_reference);

   u_foreach_bit (i, key->ps_epilog.colors_written) {
      ac_add_arg(&args->ac, AC_ARG_VGPR, 4, AC_ARG_VALUE, colors + i);
   }

   if (key->ps_epilog.writes_z)
      ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, depth);

   if (key->ps_epilog.writes_stencil)
      ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, stencil);

   if (key->ps_epilog.writes_samplemask)
      ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_VALUE, sample_mask);
}
