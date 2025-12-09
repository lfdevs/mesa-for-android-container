/* Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 */

#include "si_shader.h"
#include "si_shader_internal.h"
#include "si_pipe.h"
#include "ac_rtld.h"

/* Overview:
 * Helper utilities for handling radeonsi shader binaries.
 * Debug dumps and printing of shader keys.
 */

static const char scratch_rsrc_dword0_symbol[] = "SCRATCH_RSRC_DWORD0";
static const char scratch_rsrc_dword1_symbol[] = "SCRATCH_RSRC_DWORD1";

static bool si_shader_binary_open(struct si_screen *screen, struct si_shader *shader,
                                  struct ac_rtld_binary *rtld)
{
   const struct si_shader_selector *sel = shader->selector;
   const char *part_elfs[5];
   size_t part_sizes[5];
   unsigned num_parts = 0;

#define add_part(shader_or_part)                                                                   \
   if (shader_or_part) {                                                                           \
      assert(shader_or_part->binary.type == SI_SHADER_BINARY_ELF);                                 \
      part_elfs[num_parts] = (shader_or_part)->binary.code_buffer;                                 \
      part_sizes[num_parts] = (shader_or_part)->binary.code_size;                                  \
      num_parts++;                                                                                 \
   }

   add_part(shader->prolog);
   add_part(shader->previous_stage);
   add_part(shader);
   add_part(shader->epilog);

#undef add_part

   bool ok = ac_rtld_open(
      rtld, (struct ac_rtld_open_info){.info = &screen->info,
                                       .options =
                                          {
                                             .halt_at_entry = screen->options.halt_shaders,
                                             .waitcnt_wa = num_parts > 1 &&
                                                           screen->info.needs_llvm_wait_wa,
                                          },
                                       .shader_type = sel->stage,
                                       .wave_size = shader->wave_size,
                                       .num_parts = num_parts,
                                       .elf_ptrs = part_elfs,
                                       .elf_sizes = part_sizes});
   return ok;
}

static unsigned get_shader_binaries(struct si_shader *shader, struct si_shader_binary *bin[4])
{
   unsigned num_bin = 0;

   if (shader->prolog)
      bin[num_bin++] = &shader->prolog->binary;

   if (shader->previous_stage)
      bin[num_bin++] = &shader->previous_stage->binary;

   bin[num_bin++] = &shader->binary;

   if (shader->epilog)
      bin[num_bin++] = &shader->epilog->binary;

   return num_bin;
}

/* si_get_shader_binary_size should only be called once per shader
 * and the result should be stored in shader->complete_shader_binary_size.
 */
unsigned si_get_shader_binary_size(struct si_screen *screen, struct si_shader *shader)
{
   if (shader->binary.type == SI_SHADER_BINARY_ELF) {
      struct ac_rtld_binary rtld;
      si_shader_binary_open(screen, shader, &rtld);
      uint64_t size = rtld.exec_size;
      ac_rtld_close(&rtld);
      return size;
   } else {
      struct si_shader_binary *bin[4];
      unsigned num_bin = get_shader_binaries(shader, bin);

      unsigned size = 0;
      for (unsigned i = 0; i < num_bin; i++) {
         assert(bin[i]->type == SI_SHADER_BINARY_RAW);
         size += bin[i]->exec_size;
      }
      return size;
   }
}

static bool si_get_external_symbol(enum amd_gfx_level gfx_level, void *data, const char *name,
                                   uint64_t *value)
{
   uint64_t *scratch_va = data;

   if (!strcmp(scratch_rsrc_dword0_symbol, name)) {
      *value = (uint32_t)*scratch_va;
      return true;
   }
   if (!strcmp(scratch_rsrc_dword1_symbol, name)) {
      /* Enable scratch coalescing. */
      *value = S_008F04_BASE_ADDRESS_HI(*scratch_va >> 32);

      if (gfx_level >= GFX11)
         *value |= S_008F04_SWIZZLE_ENABLE_GFX11(1);
      else
         *value |= S_008F04_SWIZZLE_ENABLE_GFX6(1);
      return true;
   }

   return false;
}

static void *pre_upload_binary(struct si_screen *sscreen, struct si_shader *shader,
                               unsigned binary_size, bool dma_upload,
                               struct si_context **upload_ctx,
                               struct pipe_resource **staging,
                               unsigned *staging_offset,
                               int64_t bo_offset)
{
   unsigned aligned_size = ac_align_shader_binary_for_prefetch(&sscreen->info, binary_size);

   if (bo_offset >= 0) {
      /* sqtt needs to upload shaders as a pipeline, where all shaders
       * are contiguous in memory.
       * In this case, bo_offset will be positive and we don't have to
       * realloc a new bo.
       */
      shader->gpu_address = shader->bo->gpu_address + bo_offset;
      dma_upload = false;
   } else {
      si_resource_reference(&shader->bo, NULL);
      shader->bo = si_aligned_buffer_create(
         &sscreen->b,
         SI_RESOURCE_FLAG_DRIVER_INTERNAL | SI_RESOURCE_FLAG_32BIT |
         (dma_upload ? PIPE_RESOURCE_FLAG_UNMAPPABLE : 0),
         PIPE_USAGE_IMMUTABLE, align(aligned_size, SI_CPDMA_ALIGNMENT), 256);
      if (!shader->bo)
         return NULL;

      shader->gpu_address = shader->bo->gpu_address;
      bo_offset = 0;
   }

   if (dma_upload) {
      /* First upload into a staging buffer. */
      *upload_ctx = si_get_aux_context(&sscreen->aux_context.shader_upload);

      void *ret;
      u_upload_alloc_ref((*upload_ctx)->b.stream_uploader, 0, binary_size, 256,
                     staging_offset, staging, &ret);
      if (!ret)
         si_put_aux_context_flush(&sscreen->aux_context.shader_upload);

      return ret;
   } else {
      void *ptr = sscreen->ws->buffer_map(sscreen->ws,
         shader->bo->buf, NULL,
         PIPE_MAP_READ_WRITE | PIPE_MAP_UNSYNCHRONIZED | RADEON_MAP_TEMPORARY);
      if (!ptr)
         return NULL;

      return ptr + bo_offset;
   }
}

static void post_upload_binary(struct si_screen *sscreen, struct si_shader *shader,
                               void *code, unsigned code_size,
                               unsigned binary_size, bool dma_upload,
                               struct si_context *upload_ctx,
                               struct pipe_resource *staging,
                               unsigned staging_offset)
{
   if (sscreen->debug_flags & DBG(SQTT)) {
      /* Remember the uploaded code */
      shader->binary.uploaded_code_size = code_size;
      shader->binary.uploaded_code = malloc(code_size);
      memcpy(shader->binary.uploaded_code, code, code_size);
   }

   if (dma_upload) {
      /* Then copy from the staging buffer to VRAM.
       *
       * We can't use the upload copy in si_buffer_transfer_unmap because that might use
       * a compute shader, and we can't use shaders in the code that is responsible for making
       * them available.
       */
      si_cp_dma_copy_buffer(upload_ctx, &shader->bo->b.b, staging, 0, staging_offset,
                            binary_size);
      si_barrier_after_simple_buffer_op(upload_ctx, 0, &shader->bo->b.b, staging);
      upload_ctx->barrier_flags |= SI_BARRIER_INV_ICACHE | SI_BARRIER_INV_L2;

#if 0 /* debug: validate whether the copy was successful */
      uint32_t *dst_binary = malloc(binary_size);
      uint32_t *src_binary = (uint32_t*)code;
      pipe_buffer_read(&upload_ctx->b, &shader->bo->b.b, 0, binary_size, dst_binary);
      puts("dst_binary == src_binary:");
      for (unsigned i = 0; i < binary_size / 4; i++) {
         printf("   %08x == %08x\n", dst_binary[i], src_binary[i]);
      }
      free(dst_binary);
      exit(0);
#endif

      si_put_aux_context_flush(&sscreen->aux_context.shader_upload);
      pipe_resource_reference(&staging, NULL);
   } else {
      sscreen->ws->buffer_unmap(sscreen->ws, shader->bo->buf);
   }
}

static int upload_binary_elf(struct si_screen *sscreen, struct si_shader *shader,
                             uint64_t scratch_va, bool dma_upload, int64_t bo_offset)
{
   struct ac_rtld_binary binary;
   if (!si_shader_binary_open(sscreen, shader, &binary))
      return -1;

   struct si_context *upload_ctx = NULL;
   struct pipe_resource *staging = NULL;
   unsigned staging_offset = 0;

   void *rx_ptr = pre_upload_binary(sscreen, shader, binary.rx_size, dma_upload,
                                    &upload_ctx, &staging, &staging_offset,
                                    bo_offset);
   if (!rx_ptr)
      return -1;

   /* Upload. */
   struct ac_rtld_upload_info u = {};
   u.binary = &binary;
   u.get_external_symbol = si_get_external_symbol;
   u.cb_data = &scratch_va;
   u.rx_va = shader->gpu_address;
   u.rx_ptr = rx_ptr;

   int size = ac_rtld_upload(&u);

   post_upload_binary(sscreen, shader, rx_ptr, size, binary.rx_size, dma_upload,
                      upload_ctx, staging, staging_offset);

   ac_rtld_close(&binary);

   return size;
}


static int upload_binary_raw(struct si_screen *sscreen, struct si_shader *shader,
                             uint64_t scratch_va, bool dma_upload, int64_t bo_offset)
{
   struct si_shader_binary *bin[4];
   unsigned num_bin = get_shader_binaries(shader, bin);

   unsigned code_size = 0, exec_size = 0;
   for (unsigned i = 0; i < num_bin; i++) {
      assert(bin[i]->type == SI_SHADER_BINARY_RAW);
      code_size += bin[i]->code_size;
      exec_size += bin[i]->exec_size;
   }

   struct si_context *upload_ctx = NULL;
   struct pipe_resource *staging = NULL;
   unsigned staging_offset = 0;

   void *rx_ptr = pre_upload_binary(sscreen, shader, code_size, dma_upload,
                                    &upload_ctx, &staging, &staging_offset,
                                    bo_offset);
   if (!rx_ptr)
      return -1;

   unsigned exec_offset = 0, data_offset = exec_size;
   for (unsigned i = 0; i < num_bin; i++) {
      memcpy(rx_ptr + exec_offset, bin[i]->code_buffer, bin[i]->exec_size);

      if (bin[i]->num_symbols) {
         /* Offset needed to add to const data symbol because of inserting other
          * shader part between exec code and const data.
          */
         unsigned const_offset = data_offset - exec_offset - bin[i]->exec_size;

         /* Prolog and epilog have no symbols. */
         struct si_shader *sh = bin[i] == &shader->binary ? shader : shader->previous_stage;
         assert(sh && bin[i] == &sh->binary);

         si_aco_resolve_symbols(sh, rx_ptr + exec_offset, (const uint32_t *)bin[i]->code_buffer,
                                scratch_va, const_offset);
      }

      exec_offset += bin[i]->exec_size;

      unsigned data_size = bin[i]->code_size - bin[i]->exec_size;
      if (data_size) {
         memcpy(rx_ptr + data_offset, bin[i]->code_buffer + bin[i]->exec_size, data_size);
         data_offset += data_size;
      }
   }

   post_upload_binary(sscreen, shader, rx_ptr, code_size, code_size, dma_upload,
                      upload_ctx, staging, staging_offset);
   return code_size;
}

int si_shader_binary_upload_at(struct si_screen *sscreen, struct si_shader *shader,
                               uint64_t scratch_va, int64_t bo_offset)
{
   bool dma_upload = !(sscreen->debug_flags & DBG(NO_DMA_SHADERS)) && sscreen->info.has_cp_dma &&
                     sscreen->info.has_dedicated_vram && !sscreen->info.all_vram_visible &&
                     bo_offset < 0;
   int r;

   if (shader->binary.type == SI_SHADER_BINARY_ELF) {
      r = upload_binary_elf(sscreen, shader, scratch_va, dma_upload, bo_offset);
   } else {
      assert(shader->binary.type == SI_SHADER_BINARY_RAW);
      r = upload_binary_raw(sscreen, shader, scratch_va, dma_upload, bo_offset);
   }

   shader->config.lds_size = si_calculate_needed_lds_size(sscreen->info.gfx_level, shader);

   return r;
}

int si_shader_binary_upload(struct si_screen *sscreen, struct si_shader *shader,
                            uint64_t scratch_va)
{
   return si_shader_binary_upload_at(sscreen, shader, scratch_va, -1);
}

void si_shader_binary_clean(struct si_shader_binary *binary)
{
   free((void *)binary->code_buffer);
   binary->code_buffer = NULL;

   free(binary->llvm_ir_string);
   binary->llvm_ir_string = NULL;

   free((void *)binary->symbols);
   binary->symbols = NULL;

   free(binary->uploaded_code);
   binary->uploaded_code = NULL;
   binary->uploaded_code_size = 0;
}

static void print_disassembly(const char *disasm, size_t nbytes,
                              const char *name, FILE *file,
                              struct util_debug_callback *debug)
{
   if (debug && debug->debug_message) {
      /* Very long debug messages are cut off, so send the
       * disassembly one line at a time. This causes more
       * overhead, but on the plus side it simplifies
       * parsing of resulting logs.
       */
      util_debug_message(debug, SHADER_INFO, "Shader Disassembly Begin");

      uint64_t line = 0;
      while (line < nbytes) {
         int count = nbytes - line;
         const char *nl = memchr(disasm + line, '\n', nbytes - line);
         if (nl)
            count = nl - (disasm + line);

         if (count) {
            util_debug_message(debug, SHADER_INFO, "%.*s", count, disasm + line);
         }

         line += count + 1;
      }

      util_debug_message(debug, SHADER_INFO, "Shader Disassembly End");
   }

   if (file) {
      fprintf(file, "Shader %s disassembly:\n", name);
      fprintf(file, "%*s", (int)nbytes, disasm);
   }
}

static void si_shader_dump_disassembly(struct si_screen *screen,
                                       const struct si_shader_binary *binary,
                                       mesa_shader_stage stage, unsigned wave_size,
                                       struct util_debug_callback *debug, const char *name,
                                       FILE *file)
{
   if (binary->type == SI_SHADER_BINARY_RAW) {
      print_disassembly(binary->disasm_string, binary->disasm_size, name, file, debug);
      return;
   }

   struct ac_rtld_binary rtld_binary;

   if (!ac_rtld_open(&rtld_binary, (struct ac_rtld_open_info){
                                      .info = &screen->info,
                                      .shader_type = stage,
                                      .wave_size = wave_size,
                                      .num_parts = 1,
                                      .elf_ptrs = &binary->code_buffer,
                                      .elf_sizes = &binary->code_size}))
      return;

   const char *disasm;
   size_t nbytes;

   if (!ac_rtld_get_section_by_name(&rtld_binary, ".AMDGPU.disasm", &disasm, &nbytes))
      goto out;

   if (nbytes > INT_MAX)
      goto out;

   print_disassembly(disasm, nbytes, name, file, debug);

out:
   ac_rtld_close(&rtld_binary);
}

void si_shader_dump_stats_for_shader_db(struct si_screen *screen, struct si_shader *shader,
                                        struct util_debug_callback *debug)
{
   const struct ac_shader_config *conf = &shader->config;
   static const char *stages[] = {"VS", "TCS", "TES", "GS", "PS", "CS"};

   if (screen->options.debug_disassembly)
      si_shader_dump_disassembly(screen, &shader->binary, shader->selector->stage,
                                 shader->wave_size, debug, "main", NULL);

   unsigned num_ls_outputs = 0;
   unsigned num_hs_outputs = 0;
   unsigned num_es_outputs = 0;
   unsigned num_gs_outputs = 0;
   unsigned num_vs_outputs = 0;
   unsigned num_ps_outputs = 0;

   if (shader->selector->stage <= MESA_SHADER_GEOMETRY) {
      /* This doesn't include pos exports because only param exports are interesting
       * for performance and can be optimized.
       */
      if (shader->key.ge.as_ls)
         num_ls_outputs = si_shader_lshs_vertex_stride(shader) / 16;
      else if (shader->selector->stage == MESA_SHADER_TESS_CTRL)
         num_hs_outputs = shader->selector->info.tess_io_info.highest_remapped_vram_output;
      else if (shader->key.ge.as_es)
         num_es_outputs = shader->selector->info.esgs_vertex_stride / 16;
      else if (shader->gs_copy_shader)
         num_gs_outputs = shader->gs_copy_shader->info.nr_param_exports;
      else if (shader->selector->stage == MESA_SHADER_GEOMETRY)
         num_gs_outputs = shader->info.nr_param_exports;
      else if (shader->selector->stage == MESA_SHADER_VERTEX ||
               shader->selector->stage == MESA_SHADER_TESS_EVAL)
         num_vs_outputs = shader->info.nr_param_exports;
      else
         UNREACHABLE("invalid shader key");
   } else if (shader->selector->stage == MESA_SHADER_FRAGMENT) {
      num_ps_outputs = util_bitcount(shader->selector->info.colors_written) +
                       (shader->info.writes_z ||
                        shader->info.writes_stencil ||
                        shader->info.writes_sample_mask);
   }

   util_debug_message(debug, SHADER_INFO,
                      "Shader Stats: SGPRS: %d VGPRS: %d Code Size: %d "
                      "LDS: %d Scratch: %d Max Waves: %d Spilled SGPRs: %d "
                      "Spilled VGPRs: %d PrivMem VGPRs: %d LSOutputs: %u HSOutputs: %u "
                      "HSPatchOuts: %u ESOutputs: %u GSOutputs: %u VSOutputs: %u PSOutputs: %u "
                      "InlineUniforms: %u DivergentLoop: %u (%s, W%u)",
                      conf->num_sgprs, conf->num_vgprs, si_get_shader_binary_size(screen, shader),
                      align(conf->lds_size, ac_shader_get_lds_alloc_granularity(screen->info.gfx_level)),
                      conf->scratch_bytes_per_wave, shader->info.max_simd_waves,
                      conf->spilled_sgprs, conf->spilled_vgprs, shader->info.private_mem_vgprs,
                      num_ls_outputs, num_hs_outputs,
                      shader->selector->info.tess_io_info.highest_remapped_vram_patch_output,
                      num_es_outputs, num_gs_outputs, num_vs_outputs, num_ps_outputs,
                      shader->selector->info.base.num_inlinable_uniforms,
                      shader->selector->info.has_divergent_loop,
                      stages[shader->selector->stage], shader->wave_size);
}

static void si_shader_dump_stats(struct si_screen *sscreen, struct si_shader *shader, FILE *file,
                                 bool check_debug_option)
{
   const struct ac_shader_config *conf = &shader->config;

   if (shader->selector->stage == MESA_SHADER_FRAGMENT) {
      fprintf(file,
              "*** SHADER CONFIG ***\n"
              "SPI_PS_INPUT_ADDR = 0x%04x\n"
              "SPI_PS_INPUT_ENA  = 0x%04x\n",
              conf->spi_ps_input_addr, conf->spi_ps_input_ena);
   }

   fprintf(file,
           "*** SHADER STATS ***\n"
           "SGPRS: %d\n"
           "VGPRS: %d\n"
           "Spilled SGPRs: %d\n"
           "Spilled VGPRs: %d\n"
           "Private memory VGPRs: %d\n"
           "Code Size: %d bytes\n"
           "LDS: %d bytes\n"
           "Scratch: %d bytes per wave\n"
           "Max Waves: %d\n"
           "********************\n\n\n",
           conf->num_sgprs, conf->num_vgprs, conf->spilled_sgprs, conf->spilled_vgprs,
           shader->info.private_mem_vgprs, si_get_shader_binary_size(sscreen, shader),
           conf->lds_size, conf->scratch_bytes_per_wave, shader->info.max_simd_waves);
}

static void si_dump_shader_key_vs(const union si_shader_key *key, FILE *f)
{
   fprintf(f, "  mono.instance_divisor_is_one = %u\n", key->ge.mono.instance_divisor_is_one);
   fprintf(f, "  mono.instance_divisor_is_fetched = %u\n",
           key->ge.mono.instance_divisor_is_fetched);
   fprintf(f, "  mono.vs.fetch_opencode = %x\n", key->ge.mono.vs_fetch_opencode);
   fprintf(f, "  mono.vs.fix_fetch = {");
   for (int i = 0; i < SI_MAX_ATTRIBS; i++) {
      union si_vs_fix_fetch fix = key->ge.mono.vs_fix_fetch[i];
      if (i)
         fprintf(f, ", ");
      if (!fix.bits)
         fprintf(f, "0");
      else
         fprintf(f, "%u.%u.%u.%u", fix.u.reverse, fix.u.log_size, fix.u.num_channels_m1,
                 fix.u.format);
   }
   fprintf(f, "}\n");
}

static void si_dump_shader_key(const struct si_shader *shader, FILE *f)
{
   const union si_shader_key *key = &shader->key;
   mesa_shader_stage stage = shader->selector->stage;

   fprintf(f, "SHADER KEY\n");
   fprintf(f, "  source_blake3 = {");
   _mesa_blake3_print(f, shader->selector->info.base.source_blake3);
   fprintf(f, "}\n");

   switch (stage) {
   case MESA_SHADER_VERTEX:
      si_dump_shader_key_vs(key, f);
      fprintf(f, "  as_es = %u\n", key->ge.as_es);
      fprintf(f, "  as_ls = %u\n", key->ge.as_ls);
      fprintf(f, "  as_ngg = %u\n", key->ge.as_ngg);
      fprintf(f, "  mono.u.vs_export_prim_id = %u\n", key->ge.mono.u.vs_export_prim_id);
      break;

   case MESA_SHADER_TESS_CTRL:
      if (shader->selector->screen->info.gfx_level >= GFX9)
         si_dump_shader_key_vs(key, f);

      fprintf(f, "  opt.tes_prim_mode = %u\n", key->ge.opt.tes_prim_mode);
      fprintf(f, "  opt.tes_reads_tess_factors = %u\n", key->ge.opt.tes_reads_tess_factors);
      fprintf(f, "  opt.prefer_mono = %u\n", key->ge.opt.prefer_mono);
      fprintf(f, "  opt.same_patch_vertices = %u\n", key->ge.opt.same_patch_vertices);
      break;

   case MESA_SHADER_TESS_EVAL:
      fprintf(f, "  as_es = %u\n", key->ge.as_es);
      fprintf(f, "  as_ngg = %u\n", key->ge.as_ngg);
      fprintf(f, "  mono.u.vs_export_prim_id = %u\n", key->ge.mono.u.vs_export_prim_id);
      break;

   case MESA_SHADER_GEOMETRY:
      if (shader->is_gs_copy_shader)
         break;

      if (shader->selector->screen->info.gfx_level >= GFX9 &&
          key->ge.part.gs.es->stage == MESA_SHADER_VERTEX)
         si_dump_shader_key_vs(key, f);

      fprintf(f, "  mono.u.gs_tri_strip_adj_fix = %u\n", key->ge.mono.u.gs_tri_strip_adj_fix);
      fprintf(f, "  as_ngg = %u\n", key->ge.as_ngg);
      break;

   case MESA_SHADER_COMPUTE:
      break;

   case MESA_SHADER_FRAGMENT:
      fprintf(f, "  prolog.color_two_side = %u\n", key->ps.part.prolog.color_two_side);
      fprintf(f, "  prolog.flatshade_colors = %u\n", key->ps.part.prolog.flatshade_colors);
      fprintf(f, "  prolog.poly_stipple = %u\n", key->ps.part.prolog.poly_stipple);
      fprintf(f, "  prolog.force_persp_sample_interp = %u\n",
              key->ps.part.prolog.force_persp_sample_interp);
      fprintf(f, "  prolog.force_linear_sample_interp = %u\n",
              key->ps.part.prolog.force_linear_sample_interp);
      fprintf(f, "  prolog.force_persp_center_interp = %u\n",
              key->ps.part.prolog.force_persp_center_interp);
      fprintf(f, "  prolog.force_linear_center_interp = %u\n",
              key->ps.part.prolog.force_linear_center_interp);
      fprintf(f, "  prolog.bc_optimize_for_persp = %u\n",
              key->ps.part.prolog.bc_optimize_for_persp);
      fprintf(f, "  prolog.bc_optimize_for_linear = %u\n",
              key->ps.part.prolog.bc_optimize_for_linear);
      fprintf(f, "  prolog.samplemask_log_ps_iter = %u\n",
              key->ps.part.prolog.samplemask_log_ps_iter);
      fprintf(f, "  prolog.get_frag_coord_from_pixel_coord = %u\n",
              key->ps.part.prolog.get_frag_coord_from_pixel_coord);
      fprintf(f, "  prolog.force_samplemask_to_helper_invocation = %u\n",
              key->ps.part.prolog.force_samplemask_to_helper_invocation);
      fprintf(f, "  epilog.spi_shader_col_format = 0x%x\n",
              key->ps.part.epilog.spi_shader_col_format);
      fprintf(f, "  epilog.color_is_int8 = 0x%X\n", key->ps.part.epilog.color_is_int8);
      fprintf(f, "  epilog.color_is_int10 = 0x%X\n", key->ps.part.epilog.color_is_int10);
      fprintf(f, "  epilog.alpha_func = %u\n", key->ps.part.epilog.alpha_func);
      fprintf(f, "  epilog.alpha_to_one = %u\n", key->ps.part.epilog.alpha_to_one);
      fprintf(f, "  epilog.alpha_to_coverage_via_mrtz = %u\n", key->ps.part.epilog.alpha_to_coverage_via_mrtz);
      fprintf(f, "  epilog.clamp_color = %u\n", key->ps.part.epilog.clamp_color);
      fprintf(f, "  epilog.dual_src_blend_swizzle = %u\n", key->ps.part.epilog.dual_src_blend_swizzle);
      fprintf(f, "  epilog.rbplus_depth_only_opt = %u\n", key->ps.part.epilog.rbplus_depth_only_opt);
      fprintf(f, "  epilog.kill_z = %u\n", key->ps.part.epilog.kill_z);
      fprintf(f, "  epilog.kill_stencil = %u\n", key->ps.part.epilog.kill_stencil);
      fprintf(f, "  epilog.kill_samplemask = %u\n", key->ps.part.epilog.kill_samplemask);
      fprintf(f, "  mono.poly_line_smoothing = %u\n", key->ps.mono.poly_line_smoothing);
      fprintf(f, "  mono.point_smoothing = %u\n", key->ps.mono.point_smoothing);
      fprintf(f, "  mono.interpolate_at_sample_force_center = %u\n",
              key->ps.mono.interpolate_at_sample_force_center);
      fprintf(f, "  mono.fbfetch_msaa = %u\n", key->ps.mono.fbfetch_msaa);
      fprintf(f, "  mono.fbfetch_is_1D = %u\n", key->ps.mono.fbfetch_is_1D);
      fprintf(f, "  mono.fbfetch_layered = %u\n", key->ps.mono.fbfetch_layered);
      break;

   case MESA_SHADER_TASK:
   case MESA_SHADER_MESH:
      break;

   default:
      assert(0);
   }

   if ((stage == MESA_SHADER_GEOMETRY || stage == MESA_SHADER_TESS_EVAL ||
        stage == MESA_SHADER_VERTEX || stage == MESA_SHADER_MESH) &&
       !key->ge.as_es && !key->ge.as_ls) {
      fprintf(f, "  mono.remove_streamout = 0x%x\n", key->ge.mono.remove_streamout);
      fprintf(f, "  mono.write_pos_to_clipvertex = %u\n", key->ge.mono.write_pos_to_clipvertex);
      fprintf(f, "  opt.kill_outputs = 0x%" PRIx64 "\n", key->ge.opt.kill_outputs);
      fprintf(f, "  opt.kill_clip_distances = 0x%x\n", key->ge.opt.kill_clip_distances);
      fprintf(f, "  opt.kill_pointsize = %u\n", key->ge.opt.kill_pointsize);
      fprintf(f, "  opt.kill_layer = %u\n", key->ge.opt.kill_layer);
      fprintf(f, "  opt.remove_streamout = %u\n", key->ge.opt.remove_streamout);
      fprintf(f, "  opt.ngg_culling = 0x%x\n", key->ge.opt.ngg_culling);
      fprintf(f, "  opt.ngg_vs_streamout_num_verts_per_prim = %u\n",
              key->ge.opt.ngg_vs_streamout_num_verts_per_prim);
   }

   if (stage <= MESA_SHADER_GEOMETRY || stage == MESA_SHADER_MESH)
      fprintf(f, "  opt.prefer_mono = %u\n", key->ge.opt.prefer_mono);
   else
      fprintf(f, "  opt.prefer_mono = %u\n", key->ps.opt.prefer_mono);

   if (stage <= MESA_SHADER_GEOMETRY || stage == MESA_SHADER_MESH) {
      if (key->ge.opt.inline_uniforms) {
         fprintf(f, "  opt.inline_uniforms = %u (0x%x, 0x%x, 0x%x, 0x%x)\n",
                 key->ge.opt.inline_uniforms,
                 key->ge.opt.inlined_uniform_values[0],
                 key->ge.opt.inlined_uniform_values[1],
                 key->ge.opt.inlined_uniform_values[2],
                 key->ge.opt.inlined_uniform_values[3]);
      } else {
         fprintf(f, "  opt.inline_uniforms = 0\n");
      }
   } else {
      if (key->ps.opt.inline_uniforms) {
         fprintf(f, "  opt.inline_uniforms = %u (0x%x, 0x%x, 0x%x, 0x%x)\n",
                 key->ps.opt.inline_uniforms,
                 key->ps.opt.inlined_uniform_values[0],
                 key->ps.opt.inlined_uniform_values[1],
                 key->ps.opt.inlined_uniform_values[2],
                 key->ps.opt.inlined_uniform_values[3]);
      } else {
         fprintf(f, "  opt.inline_uniforms = 0\n");
      }
   }
}

const char *si_get_shader_name(const struct si_shader *shader)
{
   switch (shader->selector->stage) {
   case MESA_SHADER_VERTEX:
      if (shader->key.ge.as_es)
         return "Vertex Shader as ES";
      else if (shader->key.ge.as_ls)
         return "Vertex Shader as LS";
      else if (shader->key.ge.as_ngg)
         return "Vertex Shader as ESGS";
      else
         return "Vertex Shader as VS";
   case MESA_SHADER_TESS_CTRL:
      return "Tessellation Control Shader";
   case MESA_SHADER_TESS_EVAL:
      if (shader->key.ge.as_es)
         return "Tessellation Evaluation Shader as ES";
      else if (shader->key.ge.as_ngg)
         return "Tessellation Evaluation Shader as ESGS";
      else
         return "Tessellation Evaluation Shader as VS";
   case MESA_SHADER_GEOMETRY:
      if (shader->is_gs_copy_shader)
         return "GS Copy Shader as VS";
      else
         return "Geometry Shader";
   case MESA_SHADER_FRAGMENT:
      return "Pixel Shader";
   case MESA_SHADER_COMPUTE:
      return "Compute Shader";
   case MESA_SHADER_TASK:
      return "Task Shader";
   case MESA_SHADER_MESH:
      return "Mesh Shader";
   default:
      return "Unknown Shader";
   }
}

bool si_can_dump_shader(struct si_screen *sscreen, mesa_shader_stage stage,
                        enum si_shader_dump_type dump_type)
{
   static uint64_t filter[] = {
      [SI_DUMP_SHADER_KEY] = DBG(NIR) | DBG(INIT_LLVM) | DBG(LLVM) | DBG(INIT_ACO) | DBG(ACO) | DBG(ASM),
      [SI_DUMP_INIT_NIR] = DBG(INIT_NIR),
      [SI_DUMP_NIR] = DBG(NIR),
      [SI_DUMP_INIT_LLVM_IR] = DBG(INIT_LLVM),
      [SI_DUMP_LLVM_IR] = DBG(LLVM),
      [SI_DUMP_INIT_ACO_IR] = DBG(INIT_ACO),
      [SI_DUMP_ACO_IR] = DBG(ACO),
      [SI_DUMP_ASM] = DBG(ASM),
      [SI_DUMP_STATS] = DBG(STATS),
      [SI_DUMP_ALWAYS] = DBG(VS) | DBG(TCS) | DBG(TES) | DBG(GS) | DBG(PS) | DBG(CS) | DBG(TS) | DBG(MS),
   };
   assert(dump_type < ARRAY_SIZE(filter));

   return sscreen->shader_debug_flags & (1 << stage) &&
          sscreen->shader_debug_flags & filter[dump_type];
}

void si_shader_dump(struct si_screen *sscreen, struct si_shader *shader,
                    struct util_debug_callback *debug, FILE *file, bool check_debug_option)
{
   mesa_shader_stage stage = shader->selector->stage;

   if (!check_debug_option || si_can_dump_shader(sscreen, stage, SI_DUMP_SHADER_KEY))
      si_dump_shader_key(shader, file);

   if (!check_debug_option && shader->binary.llvm_ir_string) {
      /* This is only used with ddebug. */
      if (shader->previous_stage && shader->previous_stage->binary.llvm_ir_string) {
         fprintf(file, "\n%s - previous stage - LLVM IR:\n\n", si_get_shader_name(shader));
         fprintf(file, "%s\n", shader->previous_stage->binary.llvm_ir_string);
      }

      fprintf(file, "\n%s - main shader part - LLVM IR:\n\n", si_get_shader_name(shader));
      fprintf(file, "%s\n", shader->binary.llvm_ir_string);
   }

   if (!check_debug_option || (si_can_dump_shader(sscreen, stage, SI_DUMP_ASM))) {
      fprintf(file, "\n%s:\n", si_get_shader_name(shader));

      if (shader->prolog)
         si_shader_dump_disassembly(sscreen, &shader->prolog->binary, stage, shader->wave_size, debug,
                                    "prolog", file);
      if (shader->previous_stage)
         si_shader_dump_disassembly(sscreen, &shader->previous_stage->binary, stage,
                                    shader->wave_size, debug, "previous stage", file);
      si_shader_dump_disassembly(sscreen, &shader->binary, stage, shader->wave_size, debug, "main",
                                 file);

      if (shader->epilog)
         si_shader_dump_disassembly(sscreen, &shader->epilog->binary, stage, shader->wave_size, debug,
                                    "epilog", file);
      fprintf(file, "\n");

      si_shader_dump_stats(sscreen, shader, file, check_debug_option);
   }
}
