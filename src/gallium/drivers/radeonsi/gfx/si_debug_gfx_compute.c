/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_debug.h"
#include "ac_rtld.h"
#include "si_pipe.h"
#include "util/u_log.h"
#include "util/u_memory.h"

DEBUG_GET_ONCE_OPTION(replace_shaders, "RADEON_REPLACE_SHADERS", NULL)

static void si_dump_shader(struct si_screen *sscreen, struct si_shader *shader, FILE *f)
{
   if (shader->shader_log)
      fwrite(shader->shader_log, shader->shader_log_size, 1, f);
   else
      si_shader_dump(sscreen, shader, NULL, f, false);

   if (shader->bo && sscreen->options.dump_shader_binary) {
      unsigned size = shader->bo->b.b.width0;
      fprintf(f, "BO: VA=%" PRIx64 " Size=%u\n", shader->bo->gpu_address, size);

      const char *mapped = sscreen->ws->buffer_map(sscreen->ws,
         shader->bo->buf, NULL,
         PIPE_MAP_UNSYNCHRONIZED | PIPE_MAP_READ | RADEON_MAP_TEMPORARY);

      for (unsigned i = 0; i < size; i += 4) {
         fprintf(f, " %4x: %08x\n", i, *(uint32_t *)(mapped + i));
      }

      sscreen->ws->buffer_unmap(sscreen->ws, shader->bo->buf);

      fprintf(f, "\n");
   }
}

struct si_log_chunk_shader {
   /* The shader destroy code assumes a current context for unlinking of
    * PM4 packets etc.
    *
    * While we should be able to destroy shaders without a context, doing
    * so would happen only very rarely and be therefore likely to fail
    * just when you're trying to debug something. Let's just remember the
    * current context in the chunk.
    */
   struct si_context *ctx;
   struct si_shader *shader;

   /* For keep-alive reference counts */
   struct si_shader_selector *sel;
   struct si_compute *program;
};

static void si_log_chunk_shader_destroy(void *data)
{
   struct si_log_chunk_shader *chunk = data;
   si_shader_selector_reference(chunk->ctx, &chunk->sel, NULL);
   si_compute_reference(&chunk->program, NULL);
   FREE(chunk);
}

static void si_log_chunk_shader_print(void *data, FILE *f)
{
   struct si_log_chunk_shader *chunk = data;
   struct si_screen *sscreen = chunk->ctx->screen;
   si_dump_shader(sscreen, chunk->shader, f);
}

static struct u_log_chunk_type si_log_chunk_type_shader = {
   .destroy = si_log_chunk_shader_destroy,
   .print = si_log_chunk_shader_print,
};

static void si_dump_gfx_shader(struct si_context *ctx, const struct si_shader_ctx_state *state,
                               struct u_log_context *log)
{
   struct si_shader *current = state->current;

   if (!state->cso || !current)
      return;

   struct si_log_chunk_shader *chunk = CALLOC_STRUCT(si_log_chunk_shader);
   chunk->ctx = ctx;
   chunk->shader = current;
   si_shader_selector_reference(ctx, &chunk->sel, current->selector);
   u_log_chunk(log, &si_log_chunk_type_shader, chunk);
}

static void si_dump_compute_shader(struct si_context *ctx,
                                   struct si_compute *program,
                                   struct u_log_context *log)
{
   if (!program)
      return;

   struct si_log_chunk_shader *chunk = CALLOC_STRUCT(si_log_chunk_shader);
   chunk->ctx = ctx;
   chunk->shader = &program->shader;
   si_compute_reference(&chunk->program, program);
   u_log_chunk(log, &si_log_chunk_type_shader, chunk);
}

/**
 * Shader compiles can be overridden with arbitrary ELF objects by setting
 * the environment variable RADEON_REPLACE_SHADERS=num1:filename1[;num2:filename2]
 *
 * TODO: key this off some hash
 */
bool si_replace_shader(unsigned num, struct si_shader_binary *binary)
{
   const char *p = debug_get_option_replace_shaders();
   const char *semicolon;
   char *copy = NULL;
   FILE *f;
   long filesize, nread;
   bool replaced = false;

   if (!p)
      return false;

   while (*p) {
      unsigned long i;
      char *endp;
      i = strtoul(p, &endp, 0);

      p = endp;
      if (*p != ':') {
         mesa_loge("RADEON_REPLACE_SHADERS formatted badly.");
         exit(1);
      }
      ++p;

      if (i == num)
         break;

      p = strchr(p, ';');
      if (!p)
         return false;
      ++p;
   }
   if (!*p)
      return false;

   semicolon = strchr(p, ';');
   if (semicolon) {
      p = copy = strndup(p, semicolon - p);
      if (!copy) {
         mesa_loge("out of memory");
         return false;
      }
   }

   mesa_logi("replace shader %u by %s", num, p);

   f = fopen(p, "r");
   if (!f) {
      perror("radeonsi: failed to open file");
      goto out_free;
   }

   if (fseek(f, 0, SEEK_END) != 0)
      goto file_error;

   filesize = ftell(f);
   if (filesize < 0)
      goto file_error;

   if (fseek(f, 0, SEEK_SET) != 0)
      goto file_error;

   binary->code_buffer = MALLOC(filesize);
   if (!binary->code_buffer) {
      mesa_loge("out of memory");
      goto out_close;
   }

   nread = fread((void *)binary->code_buffer, 1, filesize, f);
   if (nread != filesize) {
      FREE((void *)binary->code_buffer);
      binary->code_buffer = NULL;
      goto file_error;
   }

   binary->type = SI_SHADER_BINARY_ELF;
   binary->code_size = nread;
   replaced = true;

out_close:
   fclose(f);
out_free:
   free(copy);
   return replaced;

file_error:
   perror("radeonsi: reading shader");
   goto out_close;
}

/* Parsed IBs are difficult to read without colors. Use "less -R file" to
 * read them, or use "aha -b -f file" to convert them to html.
 */
#define COLOR_RESET  "\033[0m"
#define COLOR_RED    "\033[31m"
#define COLOR_GREEN  "\033[1;32m"
#define COLOR_YELLOW "\033[1;33m"
#define COLOR_CYAN   "\033[1;36m"

static void si_dump_framebuffer(struct si_context *sctx, struct u_log_context *log)
{
   struct pipe_framebuffer_state *state = &sctx->framebuffer.state;
   struct si_texture *tex;
   int i;

   for (i = 0; i < state->nr_cbufs; i++) {
      if (!state->cbufs[i].texture)
         continue;

      tex = (struct si_texture *)state->cbufs[i].texture;
      u_log_printf(log, COLOR_YELLOW "Color buffer %i:" COLOR_RESET "\n", i);
      si_print_texture_info(sctx->screen, tex, log);
      u_log_printf(log, "\n");
   }

   if (state->zsbuf.texture) {
      tex = (struct si_texture *)state->zsbuf.texture;
      u_log_printf(log, COLOR_YELLOW "Depth-stencil buffer:" COLOR_RESET "\n");
      si_print_texture_info(sctx->screen, tex, log);
      u_log_printf(log, "\n");
   }
}

typedef unsigned (*slot_remap_func)(unsigned);

struct si_log_chunk_desc_list {
   /** Pointer to memory map of buffer where the list is uploader */
   uint32_t *gpu_list;
   /** Reference of buffer where the list is uploaded, so that gpu_list
    * is kept live. */
   struct si_resource *buf;

   const char *shader_name;
   const char *elem_name;
   slot_remap_func slot_remap;
   enum amd_gfx_level gfx_level;
   enum radeon_family family;
   unsigned element_dw_size;
   unsigned num_elements;

   uint32_t list[0];
};

static void si_log_chunk_desc_list_destroy(void *data)
{
   struct si_log_chunk_desc_list *chunk = data;
   si_resource_reference(&chunk->buf, NULL);
   FREE(chunk);
}

static void si_log_chunk_desc_list_print(void *data, FILE *f)
{
   struct si_log_chunk_desc_list *chunk = data;
   unsigned sq_img_rsrc_word0 =
      chunk->gfx_level >= GFX10 ? R_00A000_SQ_IMG_RSRC_WORD0 : R_008F10_SQ_IMG_RSRC_WORD0;

   for (unsigned i = 0; i < chunk->num_elements; i++) {
      unsigned cpu_dw_offset = i * chunk->element_dw_size;
      unsigned gpu_dw_offset = chunk->slot_remap(i) * chunk->element_dw_size;
      const char *list_note = chunk->gpu_list ? "GPU list" : "CPU list";
      uint32_t *cpu_list = chunk->list + cpu_dw_offset;
      uint32_t *gpu_list = chunk->gpu_list ? chunk->gpu_list + gpu_dw_offset : cpu_list;

      fprintf(f, COLOR_GREEN "%s%s slot %u (%s):" COLOR_RESET "\n", chunk->shader_name,
              chunk->elem_name, i, list_note);

      switch (chunk->element_dw_size) {
      case 4:
         for (unsigned j = 0; j < 4; j++)
            ac_dump_reg(f, chunk->gfx_level, chunk->family,
                        R_008F00_SQ_BUF_RSRC_WORD0 + j * 4, gpu_list[j], 0xffffffff);
         break;
      case 8:
         for (unsigned j = 0; j < 8; j++)
            ac_dump_reg(f, chunk->gfx_level, chunk->family,
                        sq_img_rsrc_word0 + j * 4, gpu_list[j], 0xffffffff);

         fprintf(f, COLOR_CYAN "    Buffer:" COLOR_RESET "\n");
         for (unsigned j = 0; j < 4; j++)
            ac_dump_reg(f, chunk->gfx_level, chunk->family,
                        R_008F00_SQ_BUF_RSRC_WORD0 + j * 4, gpu_list[4 + j], 0xffffffff);
         break;
      case 16:
         for (unsigned j = 0; j < 8; j++)
            ac_dump_reg(f, chunk->gfx_level,  chunk->family,
                        sq_img_rsrc_word0 + j * 4, gpu_list[j], 0xffffffff);

         fprintf(f, COLOR_CYAN "    Buffer:" COLOR_RESET "\n");
         for (unsigned j = 0; j < 4; j++)
            ac_dump_reg(f, chunk->gfx_level, chunk->family,
                        R_008F00_SQ_BUF_RSRC_WORD0 + j * 4, gpu_list[4 + j], 0xffffffff);

         fprintf(f, COLOR_CYAN "    FMASK:" COLOR_RESET "\n");
         for (unsigned j = 0; j < 8; j++)
            ac_dump_reg(f, chunk->gfx_level, chunk->family,
                        sq_img_rsrc_word0 + j * 4, gpu_list[8 + j], 0xffffffff);

         fprintf(f, COLOR_CYAN "    Sampler state:" COLOR_RESET "\n");
         for (unsigned j = 0; j < 4; j++)
            ac_dump_reg(f, chunk->gfx_level, chunk->family,
                        R_008F30_SQ_IMG_SAMP_WORD0 + j * 4, gpu_list[12 + j], 0xffffffff);
         break;
      }

      if (memcmp(gpu_list, cpu_list, chunk->element_dw_size * 4) != 0) {
         fprintf(f, COLOR_RED "!!!!! This slot was corrupted in GPU memory !!!!!" COLOR_RESET "\n");
      }

      fprintf(f, "\n");
   }
}

static const struct u_log_chunk_type si_log_chunk_type_descriptor_list = {
   .destroy = si_log_chunk_desc_list_destroy,
   .print = si_log_chunk_desc_list_print,
};

static void si_dump_descriptor_list(struct si_screen *screen, struct si_descriptors *desc,
                                    const char *shader_name, const char *elem_name,
                                    unsigned element_dw_size, unsigned num_elements,
                                    slot_remap_func slot_remap, struct u_log_context *log)
{
   if (!desc->list)
      return;

   /* In some cases, the caller doesn't know how many elements are really
    * uploaded. Reduce num_elements to fit in the range of active slots. */
   unsigned active_range_dw_begin = desc->first_active_slot * desc->element_dw_size;
   unsigned active_range_dw_end =
      active_range_dw_begin + desc->num_active_slots * desc->element_dw_size;

   while (num_elements > 0) {
      int i = slot_remap(num_elements - 1);
      unsigned dw_begin = i * element_dw_size;
      unsigned dw_end = dw_begin + element_dw_size;

      if (dw_begin >= active_range_dw_begin && dw_end <= active_range_dw_end)
         break;

      num_elements--;
   }

   struct si_log_chunk_desc_list *chunk =
      CALLOC_VARIANT_LENGTH_STRUCT(si_log_chunk_desc_list, 4 * (size_t)element_dw_size * num_elements);
   chunk->shader_name = shader_name;
   chunk->elem_name = elem_name;
   chunk->element_dw_size = element_dw_size;
   chunk->num_elements = num_elements;
   chunk->slot_remap = slot_remap;
   chunk->gfx_level = screen->info.gfx_level;
   chunk->family = screen->info.family;

   si_resource_reference(&chunk->buf, desc->buffer);
   chunk->gpu_list = desc->gpu_list;

   for (unsigned i = 0; i < num_elements; ++i) {
      memcpy(&chunk->list[i * element_dw_size], &desc->list[slot_remap(i) * element_dw_size],
             4 * element_dw_size);
   }

   u_log_chunk(log, &si_log_chunk_type_descriptor_list, chunk);
}

static unsigned si_identity(unsigned slot)
{
   return slot;
}

static void si_dump_descriptors(struct si_context *sctx, mesa_shader_stage stage,
                                const struct si_shader_info *info, struct u_log_context *log)
{
   struct si_descriptors *descs =
      &sctx->descriptors[SI_DESCS_FIRST_SHADER + stage * SI_NUM_SHADER_DESCS];
   static const char *shader_name[] = {"VS", "PS", "GS", "TCS", "TES", "CS"};
   const char *name = shader_name[stage];
   unsigned enabled_constbuf, enabled_shaderbuf, enabled_samplers;
   unsigned enabled_images;

   if (info) {
      enabled_constbuf = BITFIELD_MASK(info->base.num_ubos);
      enabled_shaderbuf = BITFIELD_MASK(info->base.num_ssbos);
      enabled_samplers = info->base.textures_used;
      enabled_images = BITFIELD_MASK(info->base.num_images);
   } else {
      enabled_constbuf =
         sctx->const_and_shader_buffers[stage].enabled_mask >> SI_NUM_SHADER_BUFFERS;
      enabled_shaderbuf = 0;
      for (int i = 0; i < SI_NUM_SHADER_BUFFERS; i++) {
         enabled_shaderbuf |=
            (sctx->const_and_shader_buffers[stage].enabled_mask &
             1llu << (SI_NUM_SHADER_BUFFERS - i - 1)) << i;
      }
      enabled_samplers = sctx->samplers[stage].enabled_mask;
      enabled_images = sctx->images[stage].enabled_mask;
   }

   si_dump_descriptor_list(sctx->screen, &descs[SI_SHADER_DESCS_CONST_AND_SHADER_BUFFERS], name,
                           " - Constant buffer", 4, util_last_bit(enabled_constbuf),
                           si_get_constbuf_slot, log);
   si_dump_descriptor_list(sctx->screen, &descs[SI_SHADER_DESCS_CONST_AND_SHADER_BUFFERS], name,
                           " - Shader buffer", 4, util_last_bit(enabled_shaderbuf),
                           si_get_shaderbuf_slot, log);
   si_dump_descriptor_list(sctx->screen, &descs[SI_SHADER_DESCS_SAMPLERS_AND_IMAGES], name,
                           " - Sampler", 16, util_last_bit(enabled_samplers), si_get_sampler_slot,
                           log);
   si_dump_descriptor_list(sctx->screen, &descs[SI_SHADER_DESCS_SAMPLERS_AND_IMAGES], name,
                           " - Image", 8, util_last_bit(enabled_images), si_get_image_slot, log);
}

static void si_dump_gfx_descriptors(struct si_context *sctx,
                                    const struct si_shader_ctx_state *state,
                                    struct u_log_context *log)
{
   if (!state->cso || !state->current)
      return;

   si_dump_descriptors(sctx, state->cso->stage, &state->cso->info, log);
}

static void si_dump_compute_descriptors(struct si_context *sctx,
                                        const struct si_compute *program,
                                        struct u_log_context *log)
{
   if (!program)
      return;

   si_dump_descriptors(sctx, program->sel.stage, NULL, log);
}

struct si_shader_inst {
   const char *text; /* start of disassembly for this instruction */
   unsigned textlen;
   unsigned size; /* instruction size = 4 or 8 */
   uint64_t addr; /* instruction address */
};

/**
 * Open the given \p binary as \p rtld_binary and split the contained
 * disassembly string into instructions and add them to the array
 * pointed to by \p instructions, which must be sufficiently large.
 *
 * Labels are considered to be part of the following instruction.
 *
 * The caller must keep \p rtld_binary alive as long as \p instructions are
 * used and then close it afterwards.
 */
static void si_add_split_disasm(struct si_screen *screen, struct ac_rtld_binary *rtld_binary,
                                struct si_shader_binary *binary, uint64_t *addr, unsigned *num,
                                struct si_shader_inst *instructions,
                                mesa_shader_stage stage, unsigned wave_size)
{
   if (!ac_rtld_open(rtld_binary, (struct ac_rtld_open_info){
                                     .gfx_level = screen->info.gfx_level,
                                     .shader_type = stage,
                                     .wave_size = wave_size,
                                     .num_parts = 1,
                                     .elf_ptrs = &binary->code_buffer,
                                     .elf_sizes = &binary->code_size}))
      return;

   const char *disasm;
   size_t nbytes;
   if (!ac_rtld_get_section_by_name(rtld_binary, ".AMDGPU.disasm", &disasm, &nbytes))
      return;

   const char *end = disasm + nbytes;
   while (disasm < end) {
      const char *semicolon = memchr(disasm, ';', end - disasm);
      if (!semicolon)
         break;

      struct si_shader_inst *inst = &instructions[(*num)++];
      const char *inst_end = memchr(semicolon + 1, '\n', end - semicolon - 1);
      if (!inst_end)
         inst_end = end;

      inst->text = disasm;
      inst->textlen = inst_end - disasm;

      inst->addr = *addr;
      /* More than 16 chars after ";" means the instruction is 8 bytes long. */
      inst->size = inst_end - semicolon > 16 ? 8 : 4;
      *addr += inst->size;

      if (inst_end == end)
         break;
      disasm = inst_end + 1;
   }
}

/* If the shader is being executed, print its asm instructions, and annotate
 * those that are being executed right now with information about waves that
 * execute them. This is most useful during a GPU hang.
 */
static void si_print_annotated_shader(struct si_shader *shader, struct ac_wave_info *waves,
                                      unsigned num_waves, FILE *f)
{
   if (!shader)
      return;

   struct si_screen *screen = shader->selector->screen;
   mesa_shader_stage stage = shader->selector->stage;
   uint64_t start_addr = shader->bo->gpu_address;
   uint64_t end_addr = start_addr + shader->bo->b.b.width0;
   unsigned i;

   /* See if any wave executes the shader. */
   for (i = 0; i < num_waves; i++) {
      if (start_addr <= waves[i].pc && waves[i].pc <= end_addr)
         break;
   }
   if (i == num_waves)
      return; /* the shader is not being executed */

   /* Remember the first found wave. The waves are sorted according to PC. */
   waves = &waves[i];
   num_waves -= i;

   /* Get the list of instructions.
    * Buffer size / 4 is the upper bound of the instruction count.
    */
   unsigned num_inst = 0;
   uint64_t inst_addr = start_addr;
   struct ac_rtld_binary rtld_binaries[5] = {};
   struct si_shader_inst *instructions =
      calloc(shader->bo->b.b.width0 / 4, sizeof(struct si_shader_inst));

   if (shader->prolog) {
      si_add_split_disasm(screen, &rtld_binaries[0], &shader->prolog->binary, &inst_addr, &num_inst,
                          instructions, stage, shader->wave_size);
   }
   if (shader->previous_stage) {
      si_add_split_disasm(screen, &rtld_binaries[1], &shader->previous_stage->binary, &inst_addr,
                          &num_inst, instructions, stage, shader->wave_size);
   }
   si_add_split_disasm(screen, &rtld_binaries[3], &shader->binary, &inst_addr, &num_inst,
                       instructions, stage, shader->wave_size);
   if (shader->epilog) {
      si_add_split_disasm(screen, &rtld_binaries[4], &shader->epilog->binary, &inst_addr, &num_inst,
                          instructions, stage, shader->wave_size);
   }

   fprintf(f, COLOR_YELLOW "%s - annotated disassembly:" COLOR_RESET "\n",
           si_get_shader_name(shader));

   /* Print instructions with annotations. */
   for (i = 0; i < num_inst; i++) {
      struct si_shader_inst *inst = &instructions[i];

      fprintf(f, "%.*s [PC=0x%" PRIx64 ", size=%u]\n", inst->textlen, inst->text, inst->addr,
              inst->size);

      /* Print which waves execute the instruction right now. */
      while (num_waves && inst->addr == waves->pc) {
         fprintf(f,
                 "          " COLOR_GREEN "^ SE%u SH%u CU%u "
                 "SIMD%u WAVE%u  EXEC=%016" PRIx64 "  ",
                 waves->se, waves->sh, waves->cu, waves->simd, waves->wave, waves->exec);

         if (inst->size == 4) {
            fprintf(f, "INST32=%08X" COLOR_RESET "\n", waves->inst_dw0);
         } else {
            fprintf(f, "INST64=%08X %08X" COLOR_RESET "\n", waves->inst_dw0, waves->inst_dw1);
         }

         waves->matched = true;
         waves = &waves[1];
         num_waves--;
      }
   }

   fprintf(f, "\n\n");
   free(instructions);
   for (unsigned i = 0; i < ARRAY_SIZE(rtld_binaries); ++i)
      ac_rtld_close(&rtld_binaries[i]);
}

void si_dump_annotated_shaders(struct si_context *sctx, FILE *f)
{
   struct ac_wave_info waves[AC_MAX_WAVES_PER_CHIP];
   unsigned num_waves = ac_get_wave_info(sctx->gfx_level, &sctx->screen->info, NULL, waves);

   fprintf(f, COLOR_CYAN "The number of active waves = %u" COLOR_RESET "\n\n", num_waves);

   si_print_annotated_shader(sctx->shader.vs.current, waves, num_waves, f);
   si_print_annotated_shader(sctx->shader.tcs.current, waves, num_waves, f);
   si_print_annotated_shader(sctx->shader.tes.current, waves, num_waves, f);
   si_print_annotated_shader(sctx->shader.gs.current, waves, num_waves, f);
   si_print_annotated_shader(sctx->shader.ps.current, waves, num_waves, f);

   /* Print waves executing shaders that are not currently bound. */
   unsigned i;
   bool found = false;
   for (i = 0; i < num_waves; i++) {
      if (waves[i].matched)
         continue;

      if (!found) {
         fprintf(f, COLOR_CYAN "Waves not executing currently-bound shaders:" COLOR_RESET "\n");
         found = true;
      }
      fprintf(f,
              "    SE%u SH%u CU%u SIMD%u WAVE%u  EXEC=%016" PRIx64 "  INST=%08X %08X  PC=%" PRIx64
              "\n",
              waves[i].se, waves[i].sh, waves[i].cu, waves[i].simd, waves[i].wave, waves[i].exec,
              waves[i].inst_dw0, waves[i].inst_dw1, waves[i].pc);
   }
   if (found)
      fprintf(f, "\n\n");
}

void si_log_draw_state(struct si_context *sctx, struct u_log_context *log)
{
   if (!log)
      return;

   si_dump_framebuffer(sctx, log);

   si_dump_compute_shader(sctx, sctx->ts_shader_state.program, log);
   si_dump_gfx_shader(sctx, &sctx->ms_shader_state, log);
   si_dump_gfx_shader(sctx, &sctx->shader.vs, log);
   si_dump_gfx_shader(sctx, &sctx->shader.tcs, log);
   si_dump_gfx_shader(sctx, &sctx->shader.tes, log);
   si_dump_gfx_shader(sctx, &sctx->shader.gs, log);
   si_dump_gfx_shader(sctx, &sctx->shader.ps, log);

   si_dump_descriptor_list(sctx->screen, &sctx->descriptors[SI_DESCS_INTERNAL], "", "RW buffers",
                           4, sctx->descriptors[SI_DESCS_INTERNAL].num_active_slots, si_identity,
                           log);
   si_dump_compute_descriptors(sctx, sctx->ts_shader_state.program, log);
   si_dump_gfx_descriptors(sctx, &sctx->ms_shader_state, log);
   si_dump_gfx_descriptors(sctx, &sctx->shader.vs, log);
   si_dump_gfx_descriptors(sctx, &sctx->shader.tcs, log);
   si_dump_gfx_descriptors(sctx, &sctx->shader.tes, log);
   si_dump_gfx_descriptors(sctx, &sctx->shader.gs, log);
   si_dump_gfx_descriptors(sctx, &sctx->shader.ps, log);
}

void si_log_compute_state(struct si_context *sctx, struct u_log_context *log)
{
   if (!log)
      return;

   si_dump_compute_shader(sctx, sctx->cs_shader_state.program, log);
   si_dump_compute_descriptors(sctx, sctx->cs_shader_state.program, log);
}

void si_gather_context_rolls(struct si_context *sctx)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   uint32_t **ibs = alloca(sizeof(ibs[0]) * (cs->num_prev + 1));
   uint32_t *ib_dw_sizes = alloca(sizeof(ib_dw_sizes[0]) * (cs->num_prev + 1));

   for (unsigned i = 0; i < cs->num_prev; i++) {
      struct ac_cmdbuf *chunk = &cs->prev[i];

      ibs[i] = chunk->buf;
      ib_dw_sizes[i] = chunk->cdw;
   }

   ibs[cs->num_prev] = cs->current.buf;
   ib_dw_sizes[cs->num_prev] = cs->current.cdw;

   FILE *f = fopen(sctx->screen->context_roll_log_filename, "a");
   ac_gather_context_rolls(f, ibs, ib_dw_sizes, cs->num_prev + 1, NULL, &sctx->screen->info);
   fclose(f);
}
