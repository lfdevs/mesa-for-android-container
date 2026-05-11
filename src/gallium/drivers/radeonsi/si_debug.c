/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_debug.h"
#include "driver_ddebug/dd_util.h"
#include "si_pipe.h"
#include "util/u_dump.h"
#include "util/u_log.h"
#include "util/u_memory.h"
#include "util/u_process.h"

static void si_dump_bo_list(struct si_context *sctx, const struct radeon_saved_cs *saved, FILE *f);

static enum amd_ip_type si_get_context_ip_type(struct si_context *sctx)
{
   return sctx->is_gfx_queue ? AMD_IP_GFX : AMD_IP_COMPUTE;
}

/**
 * Store a linearized copy of all chunks of \p cs together with the buffer
 * list in \p saved.
 */
void si_save_cs(struct radeon_winsys *ws, struct radeon_cmdbuf *cs, struct radeon_saved_cs *saved,
                bool get_buffer_list)
{
   uint32_t *buf;
   unsigned i;

   /* Save the IB chunks. */
   saved->num_dw = cs->prev_dw + cs->current.cdw;
   saved->ib = MALLOC(4 * saved->num_dw);
   if (!saved->ib)
      goto oom;

   buf = saved->ib;
   for (i = 0; i < cs->num_prev; ++i) {
      memcpy(buf, cs->prev[i].buf, cs->prev[i].cdw * 4);
      buf += cs->prev[i].cdw;
   }
   memcpy(buf, cs->current.buf, cs->current.cdw * 4);

   if (!get_buffer_list)
      return;

   /* Save the buffer list. */
   saved->bo_count = ws->cs_get_buffer_list(cs, NULL);
   saved->bo_list = CALLOC(saved->bo_count, sizeof(saved->bo_list[0]));
   if (!saved->bo_list) {
      FREE(saved->ib);
      goto oom;
   }
   ws->cs_get_buffer_list(cs, saved->bo_list);

   return;

oom:
   mesa_loge("%s: out of memory", __func__);
   memset(saved, 0, sizeof(*saved));
}

static void si_clear_saved_cs(struct radeon_saved_cs *saved)
{
   FREE(saved->ib);
   FREE(saved->bo_list);

   memset(saved, 0, sizeof(*saved));
}

void si_destroy_saved_cs(struct si_saved_cs *scs)
{
   si_clear_saved_cs(&scs->gfx);
   si_resource_reference(&scs->trace_buf, NULL);
   free(scs);
}

/* Parsed IBs are difficult to read without colors. Use "less -R file" to
 * read them, or use "aha -b -f file" to convert them to html.
 */
#define COLOR_RESET  "\033[0m"
#define COLOR_RED    "\033[31m"
#define COLOR_GREEN  "\033[1;32m"
#define COLOR_YELLOW "\033[1;33m"
#define COLOR_CYAN   "\033[1;36m"

static void si_dump_mmapped_reg(struct si_context *sctx, FILE *f, unsigned offset)
{
   struct radeon_winsys *ws = sctx->ws;
   uint32_t value;

   if (ws->read_registers(ws, offset, 1, &value))
      ac_dump_reg(f, sctx->gfx_level, sctx->family, offset, value, ~0);
}

static void si_dump_debug_registers(struct si_context *sctx, FILE *f)
{
   fprintf(f, "Memory-mapped registers:\n");
   si_dump_mmapped_reg(sctx, f, R_008010_GRBM_STATUS);

   /* No other registers can be read on radeon. */
   if (!sctx->screen->info.is_amdgpu) {
      fprintf(f, "\n");
      return;
   }

   si_dump_mmapped_reg(sctx, f, R_008008_GRBM_STATUS2);
   si_dump_mmapped_reg(sctx, f, R_008014_GRBM_STATUS_SE0);
   si_dump_mmapped_reg(sctx, f, R_008018_GRBM_STATUS_SE1);
   si_dump_mmapped_reg(sctx, f, R_008038_GRBM_STATUS_SE2);
   si_dump_mmapped_reg(sctx, f, R_00803C_GRBM_STATUS_SE3);
   si_dump_mmapped_reg(sctx, f, R_00D034_SDMA0_STATUS_REG);
   si_dump_mmapped_reg(sctx, f, R_00D834_SDMA1_STATUS_REG);
   if (sctx->gfx_level <= GFX8) {
      si_dump_mmapped_reg(sctx, f, R_000E50_SRBM_STATUS);
      si_dump_mmapped_reg(sctx, f, R_000E4C_SRBM_STATUS2);
      si_dump_mmapped_reg(sctx, f, R_000E54_SRBM_STATUS3);
   }
   si_dump_mmapped_reg(sctx, f, R_008680_CP_STAT);
   si_dump_mmapped_reg(sctx, f, R_008674_CP_STALLED_STAT1);
   si_dump_mmapped_reg(sctx, f, R_008678_CP_STALLED_STAT2);
   si_dump_mmapped_reg(sctx, f, R_008670_CP_STALLED_STAT3);
   si_dump_mmapped_reg(sctx, f, R_008210_CP_CPC_STATUS);
   si_dump_mmapped_reg(sctx, f, R_008214_CP_CPC_BUSY_STAT);
   si_dump_mmapped_reg(sctx, f, R_008218_CP_CPC_STALLED_STAT1);
   si_dump_mmapped_reg(sctx, f, R_00821C_CP_CPF_STATUS);
   si_dump_mmapped_reg(sctx, f, R_008220_CP_CPF_BUSY_STAT);
   si_dump_mmapped_reg(sctx, f, R_008224_CP_CPF_STALLED_STAT1);
   fprintf(f, "\n");
}

struct si_log_chunk_cs {
   struct si_context *ctx;
   struct si_saved_cs *cs;
   enum amd_ip_type ip_type;
   bool dump_bo_list;
   unsigned gfx_begin, gfx_end;
};

static void si_log_chunk_type_cs_destroy(void *data)
{
   struct si_log_chunk_cs *chunk = data;
   si_saved_cs_reference(&chunk->cs, NULL);
   free(chunk);
}

static void si_parse_current_ib(FILE *f, struct radeon_cmdbuf *cs, unsigned begin, unsigned end,
                                int *last_trace_id, unsigned trace_id_count,
                                enum amd_ip_type ip_type, enum amd_gfx_level gfx_level,
                                enum radeon_family family)
{
   unsigned orig_end = end;
   const char *ip_name = ac_get_ip_type_string(NULL, ip_type);

   assert(begin <= end);

   fprintf(f, "------------------ %s begin (dw = %u) ------------------\n", ip_name, begin);

   for (unsigned prev_idx = 0; prev_idx < cs->num_prev; ++prev_idx) {
      struct ac_cmdbuf *chunk = &cs->prev[prev_idx];

      if (begin < chunk->cdw) {
         struct ac_ib_parser ib_parser = {
            .f = f,
            .ib = chunk->buf + begin,
            .num_dw = MIN2(end, chunk->cdw) - begin,
            .trace_ids = last_trace_id,
            .trace_id_count = trace_id_count,
            .gfx_level = gfx_level,
            .family = family,
            .ip_type = ip_type,
         };

         ac_parse_ib_chunk(&ib_parser);
      }

      if (end <= chunk->cdw)
         return;

      if (begin < chunk->cdw)
         fprintf(f, "\n---------- %s next chunk ----------\n\n", ip_name);

      begin -= MIN2(begin, chunk->cdw);
      end -= chunk->cdw;
   }

   assert(end <= cs->current.cdw);

   struct ac_ib_parser ib_parser = {
      .f = f,
      .ib = cs->current.buf + begin,
      .num_dw = end - begin,
      .trace_ids = last_trace_id,
      .trace_id_count = trace_id_count,
      .gfx_level = gfx_level,
      .family = family,
      .ip_type = ip_type,
   };

   ac_parse_ib_chunk(&ib_parser);

   fprintf(f, "------------------- %s end (dw = %u) -------------------\n\n", ip_name, orig_end);
}

void si_print_current_ib(struct si_context *sctx, FILE *f)
{
   simple_mtx_lock(&sctx->screen->print_ib_mutex);

   si_parse_current_ib(f, &sctx->gfx_cs, 0, sctx->gfx_cs.prev_dw + sctx->gfx_cs.current.cdw,
                       NULL, 0, si_get_context_ip_type(sctx), sctx->gfx_level,
                       sctx->family);

   struct radeon_cmdbuf *gang_cs = sctx->gfx_cs.gang_cs;
   if (gang_cs) {
      unsigned end = gang_cs->prev_dw + gang_cs->current.cdw;
      if (end)
         si_parse_current_ib(f, gang_cs, 0, end, NULL, 0, AMD_IP_COMPUTE,
                             sctx->gfx_level, sctx->family);
   }

   simple_mtx_unlock(&sctx->screen->print_ib_mutex);
}

static void si_log_chunk_type_cs_print(void *data, FILE *f)
{
   struct si_log_chunk_cs *chunk = data;
   struct si_context *ctx = chunk->ctx;
   struct si_saved_cs *scs = chunk->cs;
   int last_trace_id = -1;

   /* We are expecting that the ddebug pipe has already
    * waited for the context, so this buffer should be idle.
    * If the GPU is hung, there is no point in waiting for it.
    */
   uint32_t *map = ctx->ws->buffer_map(ctx->ws, scs->trace_buf->buf, NULL,
                                       PIPE_MAP_UNSYNCHRONIZED | PIPE_MAP_READ);
   if (map)
      last_trace_id = map[0];

   if (chunk->gfx_end != chunk->gfx_begin) {
      if (scs->flushed) {
         struct ac_ib_parser ib_parser = {
            .f = f,
            .ib = scs->gfx.ib + chunk->gfx_begin,
            .num_dw = chunk->gfx_end - chunk->gfx_begin,
            .trace_ids = &last_trace_id,
            .trace_id_count = map ? 1 : 0,
            .gfx_level = ctx->gfx_level,
            .family = ctx->family,
            .ip_type = chunk->ip_type,
         };

         ac_parse_ib(&ib_parser, "IB");
      } else {
         si_parse_current_ib(f, &ctx->gfx_cs, chunk->gfx_begin, chunk->gfx_end, &last_trace_id,
                             map ? 1 : 0, chunk->ip_type, ctx->gfx_level, ctx->family);
      }
   }

   if (chunk->dump_bo_list) {
      fprintf(f, "Flushing. Time: ");
      util_dump_ns(f, scs->time_flush);
      fprintf(f, "\n\n");
      si_dump_bo_list(ctx, &scs->gfx, f);
   }
}

static const struct u_log_chunk_type si_log_chunk_type_cs = {
   .destroy = si_log_chunk_type_cs_destroy,
   .print = si_log_chunk_type_cs_print,
};

static void si_log_cs(struct si_context *ctx, struct u_log_context *log, bool dump_bo_list)
{
   assert(ctx->current_saved_cs);

   struct si_saved_cs *scs = ctx->current_saved_cs;
   unsigned gfx_cur = ctx->gfx_cs.prev_dw + ctx->gfx_cs.current.cdw;

   if (!dump_bo_list && gfx_cur == scs->gfx_last_dw)
      return;

   struct si_log_chunk_cs *chunk = calloc(1, sizeof(*chunk));

   chunk->ctx = ctx;
   si_saved_cs_reference(&chunk->cs, scs);
   chunk->ip_type = si_get_context_ip_type(ctx);
   chunk->dump_bo_list = dump_bo_list;

   chunk->gfx_begin = scs->gfx_last_dw;
   chunk->gfx_end = gfx_cur;
   scs->gfx_last_dw = gfx_cur;

   u_log_chunk(log, &si_log_chunk_type_cs, chunk);
}

void si_auto_log_cs(void *data, struct u_log_context *log)
{
   struct si_context *ctx = (struct si_context *)data;
   si_log_cs(ctx, log, false);
}

void si_log_hw_flush(struct si_context *sctx)
{
   if (!sctx->log)
      return;

   si_log_cs(sctx, sctx->log, true);

   if (sctx->context_flags & SI_CONTEXT_FLAG_AUX) {
      /* The aux context isn't captured by the ddebug wrapper,
       * so we dump it on a flush-by-flush basis here.
       */
      FILE *f = dd_get_debug_file(false);
      if (!f) {
         mesa_loge("error opening aux context dump file.");
      } else {
         dd_write_header(f, &sctx->screen->b, 0);

         fprintf(f, "Aux context dump:\n\n");
         u_log_new_page_print(sctx->log, f);

         fclose(f);
      }
   }
}

static const char *priority_to_string(unsigned priority)
{
#define ITEM(x) if (priority == RADEON_PRIO_##x) return #x
   ITEM(FENCE_TRACE);
   ITEM(SO_FILLED_SIZE);
   ITEM(QUERY);
   ITEM(IB);
   ITEM(DRAW_INDIRECT);
   ITEM(INDEX_BUFFER);
   ITEM(CP_DMA);
   ITEM(BORDER_COLORS);
   ITEM(CONST_BUFFER);
   ITEM(DESCRIPTORS);
   ITEM(SAMPLER_BUFFER);
   ITEM(VERTEX_BUFFER);
   ITEM(SHADER_RW_BUFFER);
   ITEM(SAMPLER_TEXTURE);
   ITEM(SHADER_RW_IMAGE);
   ITEM(SAMPLER_TEXTURE_MSAA);
   ITEM(COLOR_BUFFER);
   ITEM(DEPTH_BUFFER);
   ITEM(COLOR_BUFFER_MSAA);
   ITEM(DEPTH_BUFFER_MSAA);
   ITEM(SEPARATE_META);
   ITEM(SHADER_BINARY);
   ITEM(SHADER_RINGS);
   ITEM(SCRATCH_BUFFER);
#undef ITEM

   return "";
}

static int bo_list_compare_va(const struct radeon_bo_list_item *a,
                              const struct radeon_bo_list_item *b)
{
   return a->vm_address < b->vm_address ? -1 : a->vm_address > b->vm_address ? 1 : 0;
}

static void si_dump_bo_list(struct si_context *sctx, const struct radeon_saved_cs *saved, FILE *f)
{
   unsigned i, j;

   if (!saved->bo_list)
      return;

   /* Sort the list according to VM addresses first. */
   qsort(saved->bo_list, saved->bo_count, sizeof(saved->bo_list[0]), (void *)bo_list_compare_va);

   fprintf(f, "Buffer list (in units of pages = 4kB):\n" COLOR_YELLOW
              "        Size    VM start page         "
              "VM end page           Usage" COLOR_RESET "\n");

   for (i = 0; i < saved->bo_count; i++) {
      /* Note: Buffer sizes are expected to be aligned to 4k by the winsys. */
      const unsigned page_size = sctx->screen->info.gart_page_size;
      uint64_t va = saved->bo_list[i].vm_address;
      uint64_t size = saved->bo_list[i].bo_size;
      bool hit = false;

      /* If there's unused virtual memory between 2 buffers, print it. */
      if (i) {
         uint64_t previous_va_end =
            saved->bo_list[i - 1].vm_address + saved->bo_list[i - 1].bo_size;

         if (va > previous_va_end) {
            fprintf(f, "  %10" PRIu64 "    -- hole --\n", (va - previous_va_end) / page_size);
         }
      }

      /* Print the buffer. */
      fprintf(f, "  %10" PRIu64 "    0x%013" PRIX64 "       0x%013" PRIX64 "       ",
              size / page_size, va / page_size, (va + size) / page_size);

      /* Print the usage. */
      for (j = 0; j < 32; j++) {
         if (!(saved->bo_list[i].priority_usage & (1u << j)))
            continue;

         fprintf(f, "%s%s", !hit ? "" : ", ", priority_to_string(1u << j));
         hit = true;
      }
      fprintf(f, "\n");
   }
   fprintf(f, "\nNote: The holes represent memory not used by the IB.\n"
              "      Other buffers can still be allocated there.\n\n");
}

static void si_dump_command(const char *title, const char *command, FILE *f)
{
   char line[2000];

   FILE *p = popen(command, "r");
   if (!p)
      return;

   fprintf(f, COLOR_YELLOW "%s: " COLOR_RESET "\n", title);
   while (fgets(line, sizeof(line), p))
      fputs(line, f);
   fprintf(f, "\n\n");
   pclose(p);
}

static void si_dump_debug_state(struct pipe_context *ctx, FILE *f, unsigned flags)
{
   struct si_context *sctx = (struct si_context *)ctx;

   if (sctx->log)
      u_log_flush(sctx->log);

   if (flags & PIPE_DUMP_DEVICE_STATUS_REGISTERS) {
      si_dump_debug_registers(sctx, f);

      si_dump_annotated_shaders(sctx, f);
      si_dump_command("Active waves (raw data)", "umr -O halt_waves -wa | column -t", f);
      si_dump_command("Wave information", "umr -O halt_waves,bits -wa", f);
   }
}

void si_check_vm_faults(struct si_context *sctx, struct radeon_saved_cs *saved)
{
   struct pipe_screen *screen = sctx->b.screen;
   FILE *f;
   uint64_t addr;
   char cmd_line[4096];

   if (!ac_vm_fault_occurred(sctx->gfx_level, &sctx->dmesg_timestamp, &addr))
      return;

   f = dd_get_debug_file(false);
   if (!f)
      return;

   fprintf(f, "VM fault report.\n\n");
   if (util_get_command_line(cmd_line, sizeof(cmd_line)))
      fprintf(f, "Command: %s\n", cmd_line);
   fprintf(f, "Driver vendor: %s\n", screen->get_vendor(screen));
   fprintf(f, "Device vendor: %s\n", screen->get_device_vendor(screen));
   fprintf(f, "Device name: %s\n\n", screen->get_name(screen));
   fprintf(f, "Failing VM page: 0x%08" PRIx64 "\n\n", addr);

   if (sctx->apitrace_call_number)
      fprintf(f, "Last apitrace call: %u\n\n", sctx->apitrace_call_number);

   switch (si_get_context_ip_type(sctx)) {
   case AMD_IP_GFX:
   case AMD_IP_COMPUTE: {
      struct u_log_context log;
      u_log_context_init(&log);

      si_log_draw_state(sctx, &log);
      si_log_compute_state(sctx, &log);
      si_log_cs(sctx, &log, true);

      u_log_new_page_print(&log, f);
      u_log_context_destroy(&log);
      break;
   }

   default:
      break;
   }

   fclose(f);

   mesa_loge("Detected a VM fault, exiting...");
   exit(0);
}

void si_init_debug_functions(struct si_context *sctx)
{
   sctx->b.dump_debug_state = si_dump_debug_state;

   /* Set the initial dmesg timestamp for this context, so that
    * only new messages will be checked for VM faults.
    */
   if (sctx->screen->debug_flags & DBG(CHECK_VM))
      ac_vm_fault_occurred(sctx->gfx_level, &sctx->dmesg_timestamp, NULL);
}
