/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#include "si_gfx.h"
#include "si_pipe.h"
#include "si_utrace.h"

#include "amd_family.h"
#include "aco_interface.h"

#if AMD_LLVM_AVAILABLE
#include "ac_llvm_util.h"
#endif

#include "util/hash_table.h"
#include "driver_ddebug/dd_util.h"

void si_init_aux_async_compute_ctx(struct si_screen *sscreen)
{
   assert(!sscreen->async_compute_context);
   sscreen->async_compute_context =
      si_create_context(&sscreen->b,
                        SI_CONTEXT_FLAG_AUX |
                        PIPE_CONTEXT_LOSE_CONTEXT_ON_RESET |
                        (sscreen->options.aux_debug ? PIPE_CONTEXT_DEBUG : 0) |
                        PIPE_CONTEXT_COMPUTE_ONLY);

   /* Limit the numbers of waves allocated for this context. */
   if (sscreen->async_compute_context)
      ((struct si_context*)sscreen->async_compute_context)->cs_max_waves_per_sh = 2;
}

struct ac_llvm_compiler *si_create_llvm_compiler(struct si_screen *sscreen)
{
#if AMD_LLVM_AVAILABLE
   struct ac_llvm_compiler *compiler = CALLOC_STRUCT(ac_llvm_compiler);
   if (!compiler)
      return NULL;

   if (!ac_init_llvm_compiler(compiler, sscreen->info.family,
                              sscreen->shader_debug_flags & DBG(CHECK_IR) ? AC_TM_CHECK_IR : 0))
      return NULL;

   compiler->beo = ac_create_backend_optimizer(compiler->tm);
   return compiler;
#else
   return NULL;
#endif
}

void si_destroy_llvm_compiler(struct ac_llvm_compiler *compiler)
{
#if AMD_LLVM_AVAILABLE
   ac_destroy_llvm_compiler(compiler);
   FREE(compiler);
#endif
}

/* Apitrace profiling:
 *   1) qapitrace : Tools -> Profile: Measure CPU & GPU times
 *   2) In the middle panel, zoom in (mouse wheel) on some bad draw call
 *      and remember its number.
 *   3) In Mesa, enable queries and performance counters around that draw
 *      call and print the results.
 *   4) glretrace --benchmark --markers ..
 */
static void si_emit_string_marker(struct pipe_context *ctx, const char *string, int len)
{
   struct si_context *sctx = (struct si_context *)ctx;

   dd_parse_apitrace_marker(string, len, &sctx->apitrace_call_number);

   if (sctx->sqtt_enabled)
      si_write_user_event(sctx, &sctx->gfx_cs, UserEventTrigger, string, len);

   if (sctx->log)
      u_log_printf(sctx->log, "\nString marker: %*s\n", len, string);
}

bool si_init_gfx_context(struct si_screen *sscreen, struct si_context *sctx, unsigned flags)
{
   struct radeon_winsys *ws = sscreen->ws;

   /* Don't create a context if it's not compute-only and hw is compute-only. */
   if (!sscreen->info.has_graphics && !(flags & PIPE_CONTEXT_COMPUTE_ONLY)) {
      mesa_loge("can't create a graphics context on a chip that can't do graphics");
      return false;
   }

   if (!sscreen->has_gfx_compute)
      return false;

   if (sscreen->info.gfx_level >= GFX9 && sscreen->debug_flags & DBG(SQTT)) {
      /* Auto-enable stable performance profile if possible. */
      if (sscreen->b.num_contexts == 1)
         ws->cs_set_pstate(&sctx->gfx_cs, RADEON_CTX_PSTATE_PEAK);

      if (ac_check_profile_state(&sscreen->info)) {
         mesa_loge("Canceling RGP trace request as a hang condition has been "
                  "detected. Force the GPU into a profiling mode with e.g. "
                  "\"echo profile_peak  > "
                  "/sys/class/drm/card0/device/power_dpm_force_performance_level\"");
      } else {
         if (!si_init_sqtt(sctx))
            return false;

         si_handle_sqtt(sctx, &sctx->gfx_cs);
      }
   }

   sctx->is_gfx_queue = sscreen->info.gfx_level == GFX6 ||
                        /* Compute queues hang on Raven and derivatives, see:
                         * https://gitlab.freedesktop.org/mesa/mesa/-/issues/12310 */
                        ((sscreen->info.family == CHIP_RAVEN ||
                          sscreen->info.family == CHIP_RAVEN2) &&
                         !sscreen->info.has_dedicated_vram) ||
                        !sscreen->info.ip[AMD_IP_COMPUTE].num_queues ||
                        !(flags & PIPE_CONTEXT_COMPUTE_ONLY);

   if (sctx->is_gfx_queue) {
      if (sscreen->info.userq_ip_mask & (1 << AMD_IP_GFX))
         sctx->uses_userq_reg_shadowing = !(sscreen->debug_flags & DBG(USERQ_NO_SHADOW_REGS));
      else
         sctx->uses_kernelq_reg_shadowing = sscreen->info.has_kernelq_reg_shadowing;
   }

   if (flags & PIPE_CONTEXT_DEBUG)
      sscreen->record_llvm_ir = true; /* racy but not critical */

   if (sctx->gfx_level == GFX7 || sctx->gfx_level == GFX8 || sctx->gfx_level == GFX9) {
      sctx->eop_bug_scratch = si_aligned_buffer_create(
         &sscreen->b, PIPE_RESOURCE_FLAG_UNMAPPABLE | SI_RESOURCE_FLAG_DRIVER_INTERNAL,
         PIPE_USAGE_DEFAULT, 16 * sscreen->info.max_render_backends, 256);
      if (!sctx->eop_bug_scratch) {
         mesa_loge("can't create eop_bug_scratch");
         return false;
      }
   }

   if (!ws->cs_create(&sctx->gfx_cs, sctx->ctx, sctx->is_gfx_queue ? AMD_IP_GFX : AMD_IP_COMPUTE,
                      (void *)si_flush_gfx_cs, sctx)) {
      mesa_loge("can't create gfx_cs");
      sctx->gfx_cs.priv = NULL;
      return false;
   }
   assert(sctx->gfx_cs.priv);

   /* Border colors. */
   if (sscreen->info.compiler_info.has_3d_cube_border_color_mipmap) {
      sctx->border_color_table = malloc(SI_MAX_BORDER_COLORS * sizeof(*sctx->border_color_table));
      if (!sctx->border_color_table) {
         mesa_loge("can't create border_color_table");
         return false;
      }

      sctx->border_color_buffer = si_resource(pipe_buffer_create(
         &sscreen->b, 0, PIPE_USAGE_DEFAULT, SI_MAX_BORDER_COLORS * sizeof(*sctx->border_color_table)));
      if (!sctx->border_color_buffer) {
         mesa_loge("can't create border_color_buffer");
         return false;
      }

      sctx->border_color_map =
         ws->buffer_map(ws, sctx->border_color_buffer->buf, NULL, PIPE_MAP_WRITE);
      if (!sctx->border_color_map) {
         mesa_loge("can't map border_color_buffer");
         return false;
      }
   }

   sctx->shader.vs.key.ge.use_aco = 1;
   sctx->shader.gs.key.ge.use_aco = 1;
   sctx->shader.tcs.key.ge.use_aco = 1;
   sctx->shader.tes.key.ge.use_aco = 1;

   sctx->ngg = sscreen->use_ngg;

   sctx->b.emit_string_marker = si_emit_string_marker;

   si_init_all_descriptors(sctx);
   si_init_barrier_functions(sctx);
   si_init_clear_functions(sctx);
   si_init_blit_functions(sctx);
   si_init_compute_functions(sctx);
   si_init_compute_blit_functions(sctx);
   si_init_debug_functions(sctx);
   si_init_query_functions(sctx);
   si_init_state_compute_functions(sctx);

   /* Initialize graphics-only context functions. */
   if (sctx->is_gfx_queue) {
      if (sctx->gfx_level >= GFX10)
         si_gfx11_init_query(sctx);
      si_init_msaa_functions(sctx);
      si_init_shader_functions(sctx);
      si_init_state_functions(sctx);
      si_init_streamout_functions(sctx);
      si_init_viewport_functions(sctx);

      sctx->blitter = util_blitter_create(&sctx->b);
      if (sctx->blitter == NULL) {
         mesa_loge("can't create blitter");
         return false;
      }
      sctx->blitter->use_single_triangle = true;

      /* Some states are expected to be always non-NULL. */
      sctx->noop_blend = util_blitter_get_noop_blend_state(sctx->blitter);
      sctx->queued.named.blend = sctx->noop_blend;

      sctx->noop_dsa = util_blitter_get_noop_dsa_state(sctx->blitter);
      sctx->queued.named.dsa = sctx->noop_dsa;

      sctx->no_velems_state = sctx->b.create_vertex_elements_state(&sctx->b, 0, NULL);
      sctx->vertex_elements = sctx->no_velems_state;

      sctx->discard_rasterizer_state = util_blitter_get_discard_rasterizer_state(sctx->blitter);
      sctx->queued.named.rasterizer = sctx->discard_rasterizer_state;

      switch (sctx->gfx_level) {
      case GFX6:
         si_init_draw_functions_GFX6(sctx);
         break;
      case GFX7:
         si_init_draw_functions_GFX7(sctx);
         break;
      case GFX8:
         si_init_draw_functions_GFX8(sctx);
         break;
      case GFX9:
         si_init_draw_functions_GFX9(sctx);
         break;
      case GFX10:
         si_init_draw_functions_GFX10(sctx);
         break;
      case GFX10_3:
         si_init_draw_functions_GFX10_3(sctx);
         break;
      case GFX11:
         si_init_draw_functions_GFX11(sctx);
         break;
      case GFX11_5:
         si_init_draw_functions_GFX11_5(sctx);
         break;
      case GFX11_7:
         si_init_draw_functions_GFX11_7(sctx);
         break;
      case GFX12:
         si_init_draw_functions_GFX12(sctx);
         break;
      default:
         UNREACHABLE("unhandled gfx level");
      }
   }

   if (sscreen->b.caps.mesh_shader)
      si_init_task_mesh_shader_functions(sctx);

   sctx->sample_mask = 0xffff;

   /* GFX7 cannot unbind a constant buffer (S_BUFFER_LOAD doesn't skip loads
    * if NUM_RECORDS == 0). We need to use a dummy buffer instead. */
   if (sctx->gfx_level == GFX7) {
      sctx->null_const_buf.buffer =
         pipe_aligned_buffer_create(&sscreen->b,
                                    PIPE_RESOURCE_FLAG_UNMAPPABLE | SI_RESOURCE_FLAG_32BIT |
                                    SI_RESOURCE_FLAG_DRIVER_INTERNAL,
                                    PIPE_USAGE_DEFAULT, 16,
                                    sctx->screen->info.tcc_cache_line_size);
      if (!sctx->null_const_buf.buffer) {
         mesa_loge("can't create null_const_buf");
         return false;
      }
      sctx->null_const_buf.buffer_size = sctx->null_const_buf.buffer->width0;

      for (unsigned shader = 0; shader < SI_NUM_SHADERS; shader++) {
         if (!sctx->is_gfx_queue && shader != MESA_SHADER_COMPUTE)
            continue;

         for (unsigned i = 0; i < SI_NUM_CONST_BUFFERS; i++) {
            sctx->b.set_constant_buffer(&sctx->b, shader, i, &sctx->null_const_buf);
         }
      }

      si_set_internal_const_buffer(sctx, SI_HS_CONST_DEFAULT_TESS_LEVELS, &sctx->null_const_buf);
      si_set_internal_const_buffer(sctx, SI_VS_CONST_INSTANCE_DIVISORS, &sctx->null_const_buf);
      si_set_internal_const_buffer(sctx, SI_VS_CONST_CLIP_PLANES, &sctx->null_const_buf);
      si_set_internal_const_buffer(sctx, SI_PS_CONST_POLY_STIPPLE, &sctx->null_const_buf);
   }

   /* The remainder of this function initializes the gfx CS and must be last. */
   assert(sctx->gfx_cs.current.cdw == 0);

   if (!si_init_cp_reg_shadowing(sctx))
      return false;

   /* Set immutable fields of shader keys. */
   if (sctx->gfx_level >= GFX9) {
      /* The LS output / HS input layout can be communicated
       * directly instead of via user SGPRs for merged LS-HS.
       * This also enables jumping over the VS for HS-only waves.
       */
      sctx->shader.tcs.key.ge.opt.prefer_mono = 1;

      /* This enables jumping over the VS for GS-only waves. */
      sctx->shader.gs.key.ge.opt.prefer_mono = 1;
   }

   si_utrace_init(sctx);

   si_begin_new_gfx_cs(sctx, true);
   assert(sctx->gfx_cs.current.cdw == sctx->initial_gfx_cs_size);

   if (sctx->gfx_level >= GFX9 && sctx->gfx_level < GFX11) {
      sctx->wait_mem_scratch =
           si_aligned_buffer_create(&sscreen->b,
                                    PIPE_RESOURCE_FLAG_UNMAPPABLE |
                                    SI_RESOURCE_FLAG_DRIVER_INTERNAL,
                                    PIPE_USAGE_DEFAULT, 4,
                                    sscreen->info.tcc_cache_line_size);
      if (!sctx->wait_mem_scratch) {
         mesa_loge("can't create wait_mem_scratch");
         return false;
      }

      si_cp_write_data(sctx, sctx->wait_mem_scratch, 0, 4, V_371_MEMORY, V_371_MICRO_ENGINE,
                       &sctx->wait_mem_number);
   }

   if (sctx->gfx_level == GFX7) {
      /* Clear the NULL constant buffer, because loads should return zeros. */
      uint32_t clear_value = 0;
      si_cp_dma_clear_buffer(sctx, &sctx->gfx_cs, sctx->null_const_buf.buffer, 0,
                             sctx->null_const_buf.buffer->width0, clear_value);
      si_barrier_after_simple_buffer_op(sctx, 0, sctx->null_const_buf.buffer, NULL);
   }

   sctx->initial_gfx_cs_size = sctx->gfx_cs.current.cdw;
   sctx->last_timestamp_cmd = NULL;

   sctx->cs_dma_shaders = _mesa_hash_table_u64_create(NULL);
   if (!sctx->cs_dma_shaders)
      return false;

   sctx->cs_blit_shaders = _mesa_hash_table_u64_create(NULL);
   if (!sctx->cs_blit_shaders)
      return false;

   sctx->ps_resolve_shaders = _mesa_hash_table_u64_create(NULL);
   if (!sctx->ps_resolve_shaders)
      return false;

   /* Initialize compute_tmpring_size. */
   si_get_scratch_tmpring_size(sctx, 0, true, &sctx->compute_tmpring_size);

   return true;
}

void si_fini_gfx_context(struct si_context *sctx) {
   if (!si_screen(sctx->b.screen)->has_gfx_compute)
      return;

   util_queue_finish(&sctx->screen->shader_compiler_queue);
   util_queue_finish(&sctx->screen->shader_compiler_queue_opt_variants);

   if (sctx->b.set_debug_callback)
      sctx->b.set_debug_callback(&sctx->b, NULL);

   util_unreference_framebuffer_state(&sctx->framebuffer.state);
   si_release_all_descriptors(sctx);

   if (sctx->gfx_level >= GFX10 && sctx->is_gfx_queue)
      si_gfx11_destroy_query(sctx);

   if (sctx->sqtt) {
      struct si_screen *sscreen = sctx->screen;

      si_handle_sqtt(sctx, &sctx->gfx_cs);

      if (sscreen->b.num_contexts == 1 && !(sctx->context_flags & SI_CONTEXT_FLAG_AUX))
          sscreen->ws->cs_set_pstate(&sctx->gfx_cs, RADEON_CTX_PSTATE_NONE);

      si_destroy_sqtt(sctx);
   }

   si_utrace_fini(sctx);

   pipe_resource_reference(&sctx->esgs_ring, NULL);
   pipe_resource_reference(&sctx->gsvs_ring, NULL);
   pipe_resource_reference(&sctx->null_const_buf.buffer, NULL);
   si_resource_reference(&sctx->border_color_buffer, NULL);
   free(sctx->border_color_table);
   si_resource_reference(&sctx->scratch_buffer, NULL);
   si_resource_reference(&sctx->compute_scratch_buffer, NULL);
   si_resource_reference(&sctx->wait_mem_scratch, NULL);
   si_resource_reference(&sctx->wait_mem_scratch_tmz, NULL);
   si_resource_reference(&sctx->small_prim_cull_info_buf, NULL);
   si_resource_reference(&sctx->pipeline_stats_query_buf, NULL);

   if (sctx->cs_preamble_state)
      si_pm4_free_state(sctx, sctx->cs_preamble_state, ~0);
   if (sctx->cs_preamble_state_tmz)
      si_pm4_free_state(sctx, sctx->cs_preamble_state_tmz, ~0);

   if (sctx->fixed_func_tcs_shader_cache) {
      hash_table_foreach(sctx->fixed_func_tcs_shader_cache, entry) {
         sctx->b.delete_tcs_state(&sctx->b, entry->data);
      }
      _mesa_hash_table_destroy(sctx->fixed_func_tcs_shader_cache, NULL);
   }

   if (sctx->custom_dsa_flush)
      sctx->b.delete_depth_stencil_alpha_state(&sctx->b, sctx->custom_dsa_flush);
   if (sctx->custom_blend_fmask_decompress)
      sctx->b.delete_blend_state(&sctx->b, sctx->custom_blend_fmask_decompress);
   if (sctx->custom_blend_eliminate_fastclear)
      sctx->b.delete_blend_state(&sctx->b, sctx->custom_blend_eliminate_fastclear);
   if (sctx->custom_blend_dcc_decompress)
      sctx->b.delete_blend_state(&sctx->b, sctx->custom_blend_dcc_decompress);
   if (sctx->vs_blit_pos)
      sctx->b.delete_vs_state(&sctx->b, sctx->vs_blit_pos);
   if (sctx->vs_blit_pos_layered)
      sctx->b.delete_vs_state(&sctx->b, sctx->vs_blit_pos_layered);
   if (sctx->vs_blit_texcoord)
      sctx->b.delete_vs_state(&sctx->b, sctx->vs_blit_texcoord);
   if (sctx->cs_clear_buffer_rmw)
      sctx->b.delete_compute_state(&sctx->b, sctx->cs_clear_buffer_rmw);
   if (sctx->cs_ubyte_to_ushort)
      sctx->b.delete_compute_state(&sctx->b, sctx->cs_ubyte_to_ushort);
   for (unsigned i = 0; i < ARRAY_SIZE(sctx->cs_dcc_retile); i++) {
      if (sctx->cs_dcc_retile[i])
         sctx->b.delete_compute_state(&sctx->b, sctx->cs_dcc_retile[i]);
   }
   if (sctx->no_velems_state)
      sctx->b.delete_vertex_elements_state(&sctx->b, sctx->no_velems_state);

   if (sctx->global_buffers) {
      sctx->b.set_global_binding(&sctx->b, 0, sctx->max_global_buffers, NULL, NULL);
      FREE(sctx->global_buffers);
   }

   for (unsigned i = 0; i < ARRAY_SIZE(sctx->cs_fmask_expand); i++) {
      for (unsigned j = 0; j < ARRAY_SIZE(sctx->cs_fmask_expand[i]); j++) {
         if (sctx->cs_fmask_expand[i][j]) {
            sctx->b.delete_compute_state(&sctx->b, sctx->cs_fmask_expand[i][j]);
         }
      }
   }

   for (unsigned i = 0; i < ARRAY_SIZE(sctx->cs_clear_image_dcc_single); i++) {
      for (unsigned j = 0; j < ARRAY_SIZE(sctx->cs_clear_image_dcc_single[i]); j++) {
         if (sctx->cs_clear_image_dcc_single[i][j]) {
            sctx->b.delete_compute_state(&sctx->b, sctx->cs_clear_image_dcc_single[i][j]);
         }
      }
   }

   for (unsigned i = 0; i < ARRAY_SIZE(sctx->cs_clear_dcc_msaa); i++) {
      for (unsigned j = 0; j < ARRAY_SIZE(sctx->cs_clear_dcc_msaa[i]); j++) {
         for (unsigned k = 0; k < ARRAY_SIZE(sctx->cs_clear_dcc_msaa[i][j]); k++) {
            for (unsigned l = 0; l < ARRAY_SIZE(sctx->cs_clear_dcc_msaa[i][j][k]); l++) {
               for (unsigned m = 0; m < ARRAY_SIZE(sctx->cs_clear_dcc_msaa[i][j][k][l]); m++) {
                  if (sctx->cs_clear_dcc_msaa[i][j][k][l][m])
                     sctx->b.delete_compute_state(&sctx->b, sctx->cs_clear_dcc_msaa[i][j][k][l][m]);
               }
            }
         }
      }
   }

   if (sctx->blitter)
      util_blitter_destroy(sctx->blitter);

   if (sctx->query_result_shader)
      sctx->b.delete_compute_state(&sctx->b, sctx->query_result_shader);
   if (sctx->sh_query_result_shader)
      sctx->b.delete_compute_state(&sctx->b, sctx->sh_query_result_shader);

   if (sctx->gfx_cs.priv)
      sctx->ws->cs_destroy(&sctx->gfx_cs);
   if (sctx->sdma_cs) {
      sctx->ws->cs_destroy(sctx->sdma_cs);
      free(sctx->sdma_cs);
   }

   sctx->ws->fence_reference(sctx->ws, &sctx->last_gfx_fence, NULL);
   si_resource_reference(&sctx->eop_bug_scratch, NULL);
   si_resource_reference(&sctx->eop_bug_scratch_tmz, NULL);
   si_resource_reference(&sctx->shadowing.registers, NULL);
   si_resource_reference(&sctx->shadowing.csa, NULL);

   if (sctx->compiler)
      si_destroy_llvm_compiler(sctx->compiler);

   si_saved_cs_reference(&sctx->current_saved_cs, NULL);

   if (sctx->cs_dma_shaders) {
      hash_table_u64_foreach(sctx->cs_dma_shaders, entry) {
         sctx->b.delete_compute_state(&sctx->b, entry.data);
      }
      _mesa_hash_table_u64_destroy(sctx->cs_dma_shaders);
   }


   if (sctx->cs_blit_shaders) {
      hash_table_u64_foreach(sctx->cs_blit_shaders, entry) {
         sctx->b.delete_compute_state(&sctx->b, entry.data);
      }
      _mesa_hash_table_u64_destroy(sctx->cs_blit_shaders);
   }

   if (sctx->ps_resolve_shaders) {
      hash_table_u64_foreach(sctx->ps_resolve_shaders, entry) {
         sctx->b.delete_fs_state(&sctx->b, entry.data);
      }
      _mesa_hash_table_u64_destroy(sctx->ps_resolve_shaders);
   }

   si_resource_reference(&sctx->task_wait_buf, NULL);
   si_resource_reference(&sctx->task_ring, NULL);
   si_resource_reference(&sctx->task_scratch_buffer, NULL);
   si_resource_reference(&sctx->mesh_scratch_ring, NULL);
   if (sctx->task_preamble_state)
      si_pm4_free_state(sctx, sctx->task_preamble_state, ~0);
}
