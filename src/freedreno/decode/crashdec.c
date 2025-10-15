/*
 * Copyright © 2020 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

/*
 * Decoder for devcoredump traces from drm/msm.  In case of a gpu crash/hang,
 * the coredump should be found in:
 *
 *    /sys/class/devcoredump/devcd<n>/data
 *
 * The crashdump will hang around for 5min, it can be cleared by writing to
 * the file, ie:
 *
 *    echo 1 > /sys/class/devcoredump/devcd<n>/data
 *
 * (the driver won't log any new crashdumps until the previous one is cleared
 * or times out after 5min)
 */


#include "crashdec.h"
#include "cffdec.h"

static FILE *in;
bool verbose;

struct rnn *rnn_gmu;
struct rnn *rnn_control;
struct rnn *rnn_pipe;

static uint64_t fault_iova;
static bool has_fault_iova;
static int lookback = 20;

struct cffdec_options options = {
   .draw_filter = -1,
};

/*
 * Helpers to read register values:
 */

/* read registers that are 64b on 64b GPUs (ie. a5xx+) */
static uint64_t
regval64(const char *name)
{
   unsigned reg = regbase(name);
   assert(reg);
   uint64_t val = reg_val(reg);
   if (is_64b())
      val |= ((uint64_t)reg_val(reg + 1)) << 32;
   return val;
}

static uint32_t
regval(const char *name)
{
   unsigned reg = regbase(name);
   assert(reg);
   return reg_val(reg);
}

/*
 * Line reading and string helpers:
 */

static char *
replacestr(char *line, const char *find, const char *replace)
{
   char *tail, *s;

   if (!(s = strstr(line, find)))
      return line;

   tail = s + strlen(find);

   char *newline;
   asprintf(&newline, "%.*s%s%s", (int)(s - line), line, replace, tail);
   free(line);

   return newline;
}

static char *lastline;
static char *pushedline;

static void decode_finalize(void);

static const char *
popline(void)
{
   char *r = pushedline;

   if (r) {
      pushedline = NULL;
      return r;
   }

   free(lastline);

   size_t n = 0;
   if (getline(&r, &n, in) < 0) {
      decode_finalize();
      exit(0);
   }

   /* Handle section name typo's from earlier kernels: */
   r = replacestr(r, "CP_MEMPOOOL", "CP_MEMPOOL");
   r = replacestr(r, "CP_SEQ_STAT", "CP_SQE_STAT");
   r = replacestr(r, "CP_BV_SQE_STAT_ADDR", "CP_BV_SQE_STAT");

   lastline = r;
   return r;
}

static void
pushline(void)
{
   assert(!pushedline);
   pushedline = lastline;
}

static uint32_t *
popline_ascii85(uint32_t sizedwords)
{
   const char *line = popline();

   /* At this point we exepct the ascii85 data to be indented *some*
    * amount, and to terminate at the end of the line.  So just eat
    * up the leading whitespace.
    */
   assert(*line == ' ');
   while (*line == ' ')
      line++;

   uint32_t *buf = calloc(1, 4 * sizedwords);
   int idx = 0;

   while (*line != '\n') {
      if (*line == 'z') {
         buf[idx++] = 0;
         line++;
         continue;
      }

      uint32_t accum = 0;
      for (int i = 0; (i < 5) && (*line != '\n'); i++) {
         accum *= 85;
         accum += *line - '!';
         line++;
      }

      buf[idx++] = accum;
   }

   return buf;
}

static bool
startswith(const char *line, const char *start)
{
   return strstr(line, start) == line;
}

static bool
startswith_nowhitespace(const char *line, const char *start)
{
   while (*line == ' ' || *line == '\t')
      line++;
   return startswith(line, start);
}

static void
vparseline(const char *line, const char *fmt, va_list ap)
{
   int fmtlen = strlen(fmt);
   int n = 0;
   int l = 0;

   /* scan fmt string to extract expected # of conversions: */
   for (int i = 0; i < fmtlen; i++) {
      if (fmt[i] == '%') {
         if (i == (l - 1)) { /* prev char was %, ie. we have %% */
            n--;
            l = 0;
         } else {
            n++;
            l = i;
         }
      }
   }

   if (vsscanf(line, fmt, ap) != n) {
      fprintf(stderr, "parse error scanning: '%s'\n", fmt);
      exit(1);
   }
}

static void
parseline(const char *line, const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);
   vparseline(line, fmt, ap);
   va_end(ap);
}

static void
parseline_nowhitespace(const char *line, const char *fmt, ...)
{
   while (*line == ' ' || *line == '\t')
      line++;

   va_list ap;
   va_start(ap, fmt);
   vparseline(line, fmt, ap);
   va_end(ap);
}

#define foreach_line_in_section(_line)                                         \
   for (const char *_line = popline(); _line; _line = popline())               \
      /* check for start of next section */                                    \
      if (_line[0] != ' ') {                                                   \
         pushline();                                                           \
         break;                                                                \
      } else

/*
 * Helpers to decode pipe-id, etc
 */

static uint32_t
statetype_id(const char *name)
{
   if (!is_a7xx())
      return 0;
   return enumval("a7xx_statetype_id", name);
}

static uint32_t
pipe_id(const char *name)
{
   if (!name)
      return 0;
   return enumval("adreno_pipe", name);
}

static uint32_t
debugbus_id(const char *name)
{
   if (!is_a7xx())
      return 0;
   return enumval("a7xx_debugbus_id", name);
}

static uint32_t
cluster_id(const char *name)
{
   if (!is_a7xx())
      return 0;
   return enumval("a7xx_cluster", name);
}

/*
 * Decode ringbuffer section:
 */

static struct {
   uint64_t iova;
   uint32_t last_fence;
   uint32_t retired_fence;
   uint32_t rptr;
   uint32_t wptr;
   uint32_t size;
   uint32_t *buf;
} ringbuffers[5];

#include "snapshot.h"

static void
decode_ringbuffer(void)
{
   int id = 0;

   foreach_line_in_section (line) {
      if (startswith(line, "  - id:")) {
         parseline(line, "  - id: %d", &id);
         assert(id < ARRAY_SIZE(ringbuffers));
      } else if (startswith(line, "    iova:")) {
         parseline(line, "    iova: %" PRIx64, &ringbuffers[id].iova);
      } else if (startswith(line, "    last-fence:")) {
         parseline(line, "    last-fence: %u", &ringbuffers[id].last_fence);

      } else if (startswith(line, "    retired-fence:")) {
         parseline(line, "    retired-fence: %u", &ringbuffers[id].retired_fence);
      } else if (startswith(line, "    rptr:")) {
         parseline(line, "    rptr: %d", &ringbuffers[id].rptr);
      } else if (startswith(line, "    wptr:")) {
         parseline(line, "    wptr: %d", &ringbuffers[id].wptr);
      } else if (startswith(line, "    size:")) {
         parseline(line, "    size: %d", &ringbuffers[id].size);
      } else if (startswith(line, "    data: !!ascii85 |")) {
         ringbuffers[id].buf = popline_ascii85(ringbuffers[id].size / 4);
         add_buffer(ringbuffers[id].iova, ringbuffers[id].size,
                    ringbuffers[id].buf);

         int n = snapshot_linux.ctxtcount;
         if (n < ARRAY_SIZE(snapshot_contexts)) {
            snapshot_contexts[n].id = id;
            snapshot_contexts[n].timestamp_queued = ringbuffers[id].last_fence;
            snapshot_contexts[n].timestamp_consumed = ringbuffers[id].retired_fence - 1;
            snapshot_contexts[n].timestamp_retired = ringbuffers[id].retired_fence;

            snapshot_rb[n].rbsize = ringbuffers[id].size / 4;
            snapshot_rb[n].wptr = ringbuffers[id].wptr;
            snapshot_rb[n].rptr = ringbuffers[id].rptr;
            snapshot_rb[n].count = ringbuffers[id].size / 4;
            snapshot_rb[n].timestamp_queued = ringbuffers[id].last_fence;
            snapshot_rb[n].timestamp_retired = ringbuffers[id].retired_fence;
            snapshot_rb[n].gpuaddr = ringbuffers[id].iova;
            snapshot_rb[n].id = id;

            snapshot_linux.ctxtcount++;
         }

         continue;
      }

      printf("%s", line);
   }
}

/*
 * Decode GMU log
 */

static void
decode_gmu_log(void)
{
   uint64_t iova = 0;
   uint32_t size = 0;

   foreach_line_in_section (line) {
      if (startswith(line, "    iova:")) {
         parseline(line, "    iova: %" PRIx64, &iova);
      } else if (startswith(line, "    size:")) {
         parseline(line, "    size: %u", &size);
      } else if (startswith(line, "    data: !!ascii85 |")) {
         void *buf = popline_ascii85(size / 4);

         dump_hex_ascii(buf, size, 1);
         snapshot_gmu_mem(SNAPSHOT_GMU_MEM_LOG, iova, buf, size);

         free(buf);

         continue;
      }

      printf("%s", line);
   }
}

/*
 * Decode HFI queues
 */

static void
decode_gmu_hfi(void)
{
   struct a6xx_hfi_state hfi = {};

   /* Initialize the history buffers with invalid entries (-1): */
   memset(&hfi.history, 0xff, sizeof(hfi.history));

   foreach_line_in_section (line) {
      if (startswith(line, "    iova:")) {
         parseline(line, "    iova: %" PRIx64, &hfi.iova);
      } else if (startswith(line, "    size:")) {
         parseline(line, "    size: %u", &hfi.size);
      } else if (startswith(line, "    queue-history")) {
         unsigned qidx, dummy;

         parseline(line, "    queue-history[%u]:", &qidx);
         assert(qidx < ARRAY_SIZE(hfi.history));

         parseline(line, "    queue-history[%u]: %d %d %d %d %d %d %d %d", &dummy,
                   &hfi.history[qidx][0], &hfi.history[qidx][1],
                   &hfi.history[qidx][2], &hfi.history[qidx][3],
                   &hfi.history[qidx][4], &hfi.history[qidx][5],
                   &hfi.history[qidx][6], &hfi.history[qidx][7]);
      } else if (startswith(line, "    data: !!ascii85 |")) {
         hfi.buf = popline_ascii85(hfi.size / 4);

         if (verbose)
            dump_hex_ascii(hfi.buf, hfi.size, 1);

         dump_gmu_hfi(&hfi);
         snapshot_gmu_mem(SNAPSHOT_GMU_MEM_HFI, hfi.iova, hfi.buf, hfi.size);

         free(hfi.buf);

         continue;
      }

      printf("%s", line);
   }
}

static bool
valid_header(uint32_t pkt)
{
   if (options.info->chip >= 5) {
      return pkt_is_type4(pkt) || pkt_is_type7(pkt);
   } else {
      /* TODO maybe we can check validish looking pkt3 opc or pkt0
       * register offset.. the cmds sent by kernel are usually
       * fairly limited (other than initialization) which confines
       * the search space a bit..
       */
      return true;
   }
}

/**
 * Simplified version of the parsing loop in dump_commands(), which simply
 * looks for "IB" type packets and logs the target cmdstream buffers.
 */
static void
parse_ibs(uint32_t *dwords, uint32_t sizedwords)
{
   int dwords_left = sizedwords;
   uint32_t count = 0; /* dword count including packet header */
   uint32_t val;

   while (dwords_left > 0) {
      if (pkt_is_regwrite(dwords[0], &val, &count)) {
         /* ignore */
      } else if (pkt_is_opcode(dwords[0], &val, &count)) {
         const char *name = pktname(val);
         if (!strcmp(name, "CP_INDIRECT_BUFFER")) {
            uint64_t ibaddr;
            uint32_t ibsize;

            parse_cp_indirect(&dwords[1], count - 1, &ibaddr, &ibsize);

            /* map gpuaddr back to hostptr: */
            void *ptr = hostptr(ibaddr);
            snapshot_ib(ibaddr, ptr, ibsize);
         }
         // TODO CP_SET_DRAW_STATE and others?
      } else if (pkt_is_type2(dwords[0])) {
         /* no-op */
         count = 1;
      } else {
         printf("bad type! %08x\n", dwords[0]);
         /* for 5xx+ we can do a passable job of looking for start of next valid
          * packet: */
         if (options.info->chip >= 5) {
            count = find_next_packet(dwords, dwords_left);
         } else {
            return;
         }
      }

      dwords += count;
      dwords_left -= count;
   }
}

static void
dump_cmdstream(void)
{
   uint64_t rb_base = regval64("CP_RB_BASE");

   printf("got rb_base=%" PRIx64 "\n", rb_base);

   options.ibs[1].base = regval64("CP_IB1_BASE");
   if (have_rem_info())
      options.ibs[1].rem = regval("CP_IB1_REM_SIZE");
   options.ibs[2].base = regval64("CP_IB2_BASE");
   if (have_rem_info())
      options.ibs[2].rem = regval("CP_IB2_REM_SIZE");
   uint32_t rb_rptr = regval("CP_RB_RPTR");

   /* Adjust remaining size to account for cmdstream slurped into ROQ
    * but not yet consumed by SQE
    *
    * TODO add support for earlier GPUs once we tease out the needed
    * registers.. see crashit.c in msmtest for hints.
    *
    * TODO it would be nice to be able to extract out register bitfields
    * by name rather than hard-coding this.
    */
   uint32_t rb_rem = 0;
   if (have_rem_info()) {
      uint32_t ib1_rem = regval("CP_ROQ_AVAIL_IB1") >> 16;
      uint32_t ib2_rem = regval("CP_ROQ_AVAIL_IB2") >> 16;
      rb_rem = regval("CP_ROQ_AVAIL_RB") >> 16;
      options.ibs[1].rem += ib1_rem ? ib1_rem - 1 : 0;
      options.ibs[2].rem += ib2_rem ? ib2_rem - 1 : 0;
   }

   printf("IB1: %" PRIx64 ", %u\n", options.ibs[1].base, options.ibs[1].rem);
   printf("IB2: %" PRIx64 ", %u\n", options.ibs[2].base, options.ibs[2].rem);

   /* now that we've got the regvals we want, reset register state
    * so we aren't seeing values from decode_registers();
    */
   reset_regs();

   for (int id = 0; id < ARRAY_SIZE(ringbuffers); id++) {
      if (ringbuffers[id].iova != rb_base)
         continue;
      if (!ringbuffers[id].size)
         continue;

      printf("found ring!\n");

      /* The kernel level ringbuffer (RB) wraps around, which
       * cffdec doesn't really deal with.. so figure out how
       * many dwords are unread
       */
      unsigned ringszdw = ringbuffers[id].size >> 2; /* in dwords */

      if (verbose) {
         handle_prefetch(ringbuffers[id].buf, ringszdw);
         dump_commands(ringbuffers[id].buf, ringszdw, 0);
         return;
      }

/* helper macro to deal with modulo size math: */
#define mod_add(b, v) ((ringszdw + (int)(b) + (int)(v)) % ringszdw)

      /* On a7xx, the RPTR seems to be the point the SQE is reading, and on
       * a6xx it is the point the ROQ is reading. We really care about where
       * the SQE is reading, so back it up on a6xx.
       */
      if (is_a6xx())
         rb_rptr = mod_add(rb_rptr, -rb_rem);

      /* The rptr will (most likely) have moved past the IB to
       * userspace cmdstream, so back up a bit, and then advance
       * until we find a valid start of a packet.. this is going
       * to be less reliable on a4xx and before (pkt0/pkt3),
       * compared to pkt4/pkt7 with parity bits
       */
      unsigned rptr = mod_add(rb_rptr, -lookback);

      for (int idx = 0; idx < lookback; idx++) {
         if (valid_header(ringbuffers[id].buf[rptr]))
            break;
         rptr = mod_add(rptr, 1);
      }

      unsigned cmdszdw = mod_add(ringbuffers[id].wptr, -rptr);

      printf("got cmdszdw=%d\n", cmdszdw);
      uint32_t *buf = malloc(cmdszdw * 4);

      for (int idx = 0; idx < cmdszdw; idx++) {
         int p = mod_add(rptr, idx);
         buf[idx] = ringbuffers[id].buf[p];
      }

      options.rb_host_base = buf;
      options.ibs[0].rem = mod_add(ringbuffers[id].wptr, -rb_rptr);
      options.ibs[0].size = cmdszdw;

      handle_prefetch(buf, cmdszdw);

      if (snapshot) {
         parse_ibs(buf, cmdszdw);
      } else {
         dump_commands(buf, cmdszdw, 0);
      }

      free(buf);
   }
}

/*
 * Decode optional 'fault-info' section.  We only get this section if
 * the devcoredump was triggered by an iova fault:
 */

static void
decode_fault_info(void)
{
   foreach_line_in_section (line) {
      if (startswith(line, "  - far:")) {
         parseline(line, "  - far: %" PRIx64, &fault_iova);
         has_fault_iova = true;
      } else if (startswith(line, "  - iova=")) {
         parseline(line, "  - iova=%" PRIx64, &fault_iova);
         has_fault_iova = true;
      }

      printf("%s", line);
   }
}

/*
 * Decode 'bos' (buffers) section:
 */

static void
decode_bos(void)
{
   uint32_t size = 0;
   uint64_t iova = 0;

   foreach_line_in_section (line) {
      if (startswith(line, "  - iova:")) {
         parseline(line, "  - iova: %" PRIx64, &iova);
         continue;
      } else if (startswith(line, "    size:")) {
         parseline(line, "    size: %u", &size);

         /*
          * This is a bit convoluted, vs just printing the lines as
          * they come.  But we want to have both the iova and size
          * so we can print the end address of the buffer
          */

         uint64_t end = iova + size;

         printf("  - iova: 0x%016" PRIx64 "-0x%016" PRIx64, iova, end);

         if (has_fault_iova) {
            if ((iova <= fault_iova) && (fault_iova < end)) {
               /* Fault address was within what should be a mapped buffer!! */
               printf("\t==");
            } else if ((iova <= fault_iova) && (fault_iova < (end + size))) {
               /* Fault address was near this mapped buffer */
               printf("\t>=");
            }
         }
         printf("\n");
         printf("    size: %u (0x%x)\n", size, size);
         continue;
      } else if (startswith(line, "    data: !!ascii85 |")) {
         uint32_t *buf = popline_ascii85(size / 4);

         if (verbose)
            dump_hex_ascii(buf, size, 1);

         add_buffer(iova, size, buf);

         if (size <= 0x40000)
            snapshot_gpu_object(iova, size, buf);

         continue;
      }

      printf("%s", line);
   }
}

/*
 * Decode registers section:
 */

void
dump_register(struct regacc *r)
{
   struct rnndecaddrinfo *info = rnn_reginfo(r->rnn, r->regbase);
   if (info && info->typeinfo) {
      char *decoded = rnndec_decodeval(r->rnn->vc, info->typeinfo, r->value);
      printf("%s: %s\n", info->name, decoded);
   } else if (info) {
      printf("%s: %08"PRIx64"\n", info->name, r->value);
   } else {
      printf("<%04x>: %08"PRIx64"\n", r->regbase, r->value);
   }
   rnn_reginfo_free(info);
}

static void
decode_gmu_registers(void)
{
   struct regacc r = regacc(rnn_gmu);

   foreach_line_in_section (line) {
      uint32_t offset, value;
      parseline(line, "  - { offset: %x, value: %x }", &offset, &value);

      assert(reg_buf.count < ARRAY_SIZE(reg_buf.regs));

      reg_buf.regs[reg_buf.count].offset = offset / 4;
      reg_buf.regs[reg_buf.count].value = value;
      reg_buf.count++;

      if (regacc_push(&r, offset / 4, value)) {
         printf("\t%08"PRIx64"\t", r.value);
         dump_register(&r);
      }
   }

   snapshot_registers();
}

static void
decode_registers(void)
{
   struct regacc r = regacc(NULL);

   foreach_line_in_section (line) {
      uint32_t offset, value;
      parseline(line, "  - { offset: %x, value: %x }", &offset, &value);

      assert(reg_buf.count < ARRAY_SIZE(reg_buf.regs));

      reg_buf.regs[reg_buf.count].offset = offset / 4;
      reg_buf.regs[reg_buf.count].value = value;
      reg_buf.count++;

      reg_set(offset / 4, value);
      if (regacc_push(&r, offset / 4, value)) {
         printf("\t%08"PRIx64, r.value);
         dump_register_val(&r, 0);
      }
   }

   snapshot_registers();
}

/* similar to registers section, but for banked context regs: */
static void
decode_clusters(void)
{
   struct regacc r = regacc(NULL);
   char *cluster_name = NULL;
   char *pipe_name = NULL;
   uint32_t context = 0;
   uint32_t location = ~0;

   foreach_line_in_section (line) {
      if (startswith_nowhitespace(line, "- cluster-name:")) {
         free(cluster_name);
         parseline_nowhitespace(line, "- cluster-name: %ms", &cluster_name);
         location = ~0;
      } else if (startswith_nowhitespace(line, "- context:")) {
         parseline_nowhitespace(line, "- context: %u", &context);
      } else if (startswith_nowhitespace(line, "- location:")) {
         parseline_nowhitespace(line, "- location: %u", &location);
      } else if (startswith_nowhitespace(line, "- pipe:")) {
         snapshot_cluster_regs(pipe_id(pipe_name), cluster_id(cluster_name),
                               context, location);

         free(pipe_name);
         parseline_nowhitespace(line, "- pipe: %ms", &pipe_name);
      } else {
         uint32_t offset, value;
         parseline_nowhitespace(line, "- { offset: %x, value: %x }", &offset, &value);

         assert(reg_buf.count < ARRAY_SIZE(reg_buf.regs));

         reg_buf.regs[reg_buf.count].offset = offset / 4;
         reg_buf.regs[reg_buf.count].value = value;
         reg_buf.count++;

         if (regacc_push(&r, offset / 4, value)) {
            printf("\t%08"PRIx64, r.value);
            dump_register_val(&r, 0);
         }

         continue;
      }
      printf("%s", line);
   }

   if (reg_buf.count) {
      snapshot_cluster_regs(pipe_id(pipe_name), cluster_id(cluster_name),
                            context, location);
   }

   free(cluster_name);
   free(pipe_name);
}

/*
 * Decode indexed-registers.. these aren't like normal registers, but a
 * sort of FIFO where successive reads pop out associated debug state.
 */

static void
dump_cp_sqe_stat(uint32_t *stat)
{
   printf("\t PC: %04x\n", stat[0]);
   stat++;

   if (!is_a5xx() && valid_header(stat[0])) {
      if (pkt_is_type7(stat[0])) {
         unsigned opc = cp_type7_opcode(stat[0]);
         const char *name = pktname(opc);
         if (name)
            printf("\tPKT: %s\n", name);
      } else {
         /* Not sure if this case can happen: */
      }
   }

   for (int i = 0; i < 16; i++) {
      printf("\t$%02x: %08x\t\t$%02x: %08x\n", i + 1, stat[i], i + 16 + 1,
             stat[i + 16]);
   }
}

static void
dump_scratch_control_regs(uint32_t *regs)
{
   if (!rnn_control)
      return;

   struct regacc r = regacc(rnn_control);

   /* Control regs 0x100-0x17f are a scratch space to be used by the firmware
    * however it wants, unlike lower regs which involve some fixed-function
    * units. Therefore only these registers get dumped directly. On a7xx this
    * is doubled to 0x100-0x1ff, and on a7xx gen3 this is shuffled to
    * 0x400-0x4ff to make space for expanded shared regs.
    */
   uint32_t scratch_size = is_a7xx() ? 0x100 : 0x80;
   uint32_t scratch_base = has_a7xx_gen3_control_regs() ? 0x400 : 0x100;

   for (uint32_t i = 0; i < scratch_size; i++) {
      if (regacc_push(&r, i + scratch_base, regs[i])) {
         printf("\t%08"PRIx64"\t", r.value);
         dump_register(&r);
      }
   }
}

static void
dump_control_regs(uint32_t *regs)
{
   if (!rnn_control)
      return;

   struct regacc r = regacc(rnn_control);

   for (uint32_t i = 0; i < 0x400; i++) {
      if (regacc_push(&r, i, regs[i])) {
         printf("\t%08"PRIx64"\t", r.value);
         dump_register(&r);
      }
   }
}

static void
dump_cp_ucode_dbg(uint32_t *dbg)
{
   /* Notes on the data:
    * There seems to be a section every 4096 DWORD's. The sections aren't
    * all the same size, so the rest of the 4096 DWORD's are filled with
    * mirrors of the actual data.
    */

   for (int section = 0; section < (has_a7xx_gen3_control_regs() ? 8 : 6); section++, dbg += 0x1000) {
      switch (section) {
      case 0:
         /* Contains scattered data from a630_sqe.fw: */
         printf("\tSQE instruction cache:\n");
         dump_hex_ascii(dbg, 4 * 0x400, 1);
         break;
      case 1:
         printf("\tUnknown 1:\n");
         dump_hex_ascii(dbg, 4 * 0x80, 1);
         break;
      case 2:
         printf("\tUnknown 2:\n");
         dump_hex_ascii(dbg, 4 * 0x200, 1);
         break;
      case 3:
         printf("\tUnknown 3:\n");
         dump_hex_ascii(dbg, 4 * 0x80, 1);
         break;
      case 4:
         /* Don't bother printing this normally */
         if (verbose) {
            printf("\tSQE packet jumptable contents:\n");
            dump_hex_ascii(dbg, 4 * 0x80, 1);
         }
         break;
      case 5:
         printf("\tSQE scratch control regs:\n");
         dump_scratch_control_regs(dbg);
         break;
      /* TODO check if this exists prior to a750 */
      case 7:
         printf("\tSQE control regs:\n");
         dump_control_regs(dbg);
         break;
      }
   }
}

const static struct {
   const char *from;
   const char *to;
} index_reg_renames[] = {
   {"CP_ROQ", "CP_ROQ_DBG"},
   {"CP_UCODE_DBG_DATA", "CP_SQE_UCODE_DBG"},
   {"CP_UCODE_DBG", "CP_SQE_UCODE_DBG"},
   {"CP_RESOURCE_TBL", "CP_RESOURCE_TABLE_DBG"},
   {"CP_LPAC_ROQ", "CP_LPAC_ROQ_DBG"},
   {"CP_BV_DRAW_STATE_ADDR", "CP_BV_DRAW_STATE"},
   {"CP_BV_ROQ_DBG_ADDR", "CP_BV_ROQ_DBG"},
   {"CP_BV_SQE_UCODE_DBG_ADDR", "CP_BV_SQE_UCODE_DBG"},
   {"CP_LPAC_DRAW_STATE_ADDR", "CP_LPAC_DRAW_STATE"},
   {"CP_SQE_AC_UCODE_DBG_ADDR", "CP_SQE_AC_UCODE_DBG"},
   {"CP_SQE_AC_STAT_ADDR", "CP_SQE_AC_STAT"},
   {"CP_LPAC_FIFO_DBG_ADDR", "CP_LPAC_FIFO_DBG"},
   {"CP_MEMPOOL", "CP_MEM_POOL_DBG"},
   {"CP_BV_MEMPOOL", "CP_BV_MEM_POOL_DBG"},
};

static void
decode_indexed_registers(void)
{
   char *name = NULL;
   uint32_t sizedwords = 0;

   foreach_line_in_section (line) {
      if (startswith(line, "  - regs-name:")) {
         free(name);
         parseline(line, "  - regs-name: %ms", &name);

         /* kernel is inconsitent, sometimes the name ends in _DATA or _ADDR,
          * or various other renaming:
          */
         for (int i = 0; i < ARRAY_SIZE(index_reg_renames); i++) {
            if (!strcmp(name, index_reg_renames[i].from)) {
               free(name);
               name = strdup(index_reg_renames[i].to);
            }
         }
      } else if (startswith(line, "    dwords:")) {
         parseline(line, "    dwords: %u", &sizedwords);
      } else if (startswith(line, "    data: !!ascii85 |")) {
         uint32_t *buf = popline_ascii85(sizedwords);

         /* some of the sections are pretty large, and are (at least
          * so far) not useful, so skip them if not in verbose mode:
          */
         bool dump = verbose || !strcmp(name, "CP_SQE_STAT") ||
                     !strcmp(name, "CP_BV_SQE_STAT") ||
                     !strcmp(name, "CP_DRAW_STATE") ||
                     !strcmp(name, "CP_ROQ_DBG") || 0;

         if (!strcmp(name, "CP_SQE_STAT") || !strcmp(name, "CP_BV_SQE_STAT"))
            dump_cp_sqe_stat(buf);

         if (!strcmp(name, "CP_SQE_UCODE_DBG") ||
             !strcmp(name, "CP_BV_SQE_UCODE_DBG"))
            dump_cp_ucode_dbg(buf);

         if (!strcmp(name, "CP_MEM_POOL_DBG"))
            dump_cp_mem_pool(buf, false);
         if (!strcmp(name, "CP_BV_MEM_POOL_DBG"))
            dump_cp_mem_pool(buf, true);

         if (dump)
            dump_hex_ascii(buf, 4 * sizedwords, 1);

         snapshot_indexed_regs(name, buf, sizedwords);

         free(buf);

         continue;
      }

      printf("%s", line);
   }
}

/*
 * Decode shader-blocks:
 */

static void
decode_shader_blocks(void)
{
   char *type = NULL;
   char *pipe = NULL;
   int sp = 0;
   int usptp = 0;
   /* NOTE: earlier kernels do not report the location.  But conveniently
    * all entries before A7XX_HLSQ_DATAPATH_DSTR_META are USPTP (3) and
    * the other entries are HLSQ_STATE (0), so we can implement a work-
    * around.
    */
   int location = 3;  /* A7XX_USPTP */
   uint32_t sizedwords = 0;

   foreach_line_in_section (line) {
      if (startswith(line, "  - type:")) {
         free(type);
         parseline(line, "  - type: %ms", &type);
         if (!strcmp(type, "A7XX_HLSQ_DATAPATH_DSTR_META"))
            location = 0;  /* A7XX_HLSQ_STATE */
      } else if (startswith_nowhitespace(line, "- pipe:")) {
         free(pipe);
         parseline_nowhitespace(line, "- pipe: %ms", &pipe);
      } else if (startswith_nowhitespace(line, "- location:")) {
         parseline_nowhitespace(line, "- location: %d", &location);
      } else if (startswith_nowhitespace(line, "- sp:")) {
         parseline_nowhitespace(line, "- sp: %d", &sp);
      } else if (startswith_nowhitespace(line, "- usptp:")) {
         parseline_nowhitespace(line, "- usptp: %d", &usptp);
      } else if (startswith_nowhitespace(line, "size:")) {
         parseline_nowhitespace(line, "size: %u", &sizedwords);
      } else if (startswith_nowhitespace(line, "data: !!ascii85 |")) {
         uint32_t *buf = popline_ascii85(sizedwords);

         /* some of the sections are pretty large, and are (at least
          * so far) not useful, so skip them if not in verbose mode:
          */
         bool dump = verbose || !strcmp(type, "A6XX_SP_INST_DATA") ||
                     !strcmp(type, "A6XX_HLSQ_INST_RAM") ||
                     !strcmp(type, "A7XX_SP_INST_DATA") ||
                     !strcmp(type, "A7XX_HLSQ_INST_RAM") || 0;

         if (!strcmp(type, "A6XX_SP_INST_DATA") ||
             !strcmp(type, "A6XX_HLSQ_INST_RAM") ||
             !strcmp(type, "A7XX_SP_INST_DATA") ||
             !strcmp(type, "A7XX_HLSQ_INST_RAM")) {
            /* TODO this section actually contains multiple shaders
             * (or parts of shaders?), so perhaps we should search
             * for ends of shaders and decode each?
             */
            try_disasm_a3xx(buf, sizedwords, 1, stdout, options.info->chip * 100);
         }

         if (dump)
            dump_hex_ascii(buf, 4 * sizedwords, 1);

         snapshot_shader_block(statetype_id(type), pipe_id(pipe),
                               sp, usptp, location, buf, sizedwords);

         free(buf);

         continue;
      }

      printf("%s", line);
   }

   free(type);
   free(pipe);
}

/*
 * Decode debugbus section:
 */

static void
decode_debugbus(void)
{
   char *block = NULL;
   uint32_t sizedwords = 0;

   foreach_line_in_section (line) {
      if (startswith(line, "  - debugbus-block:")) {
         free(block);
         parseline(line, "  - debugbus-block: %ms", &block);
      } else if (startswith(line, "    count:")) {
         parseline(line, "    count: %u", &sizedwords);
      } else if (startswith(line, "    data: !!ascii85 |")) {
         uint32_t *buf = popline_ascii85(sizedwords);

         /* some of the sections are pretty large, and are (at least
          * so far) not useful, so skip them if not in verbose mode:
          */
         bool dump = verbose || 0;

         if (dump)
            dump_hex_ascii(buf, 4 * sizedwords, 1);
         snapshot_debugbus(debugbus_id(block), buf, sizedwords);

         free(buf);

         continue;
      }

      printf("%s", line);
   }
}

/*
 * Main crashdump decode loop:
 */

static void
decode(void)
{
   const char *line;

   while ((line = popline())) {
      printf("%s", line);
      if (startswith(line, "kernel:")) {
         char *release = NULL;

         parseline(line, "kernel: %ms", &release);
         strncpy((char *)snapshot_linux.release, release, sizeof(snapshot_linux.release) - 1);
         free(release);
      } else if (startswith(line, "time:")) {
         double time;

         parseline(line, "time: %d", &time);
         snapshot_linux.seconds = (uint32_t)time;
      } else if (startswith(line, "revision:")) {
         unsigned core, major, minor, patchid;

         parseline(line, "revision: %u (%u.%u.%u.%u)", &options.dev_id.gpu_id,
                   &core, &major, &minor, &patchid);

         options.dev_id.chip_id = (core << 24) | (major << 16) | (minor << 8) | patchid;
         options.info = fd_dev_info_raw(&options.dev_id);
         if (!options.info) {
            printf("Unsupported device\n");
            break;
         }

         printf("Got chip_id=0x%"PRIx64"\n", options.dev_id.chip_id);

         cffdec_init(&options);

         if (is_a7xx()) {
            rnn_gmu = rnn_new(!options.color);
            rnn_load_file(rnn_gmu, "adreno/a6xx_gmu.xml", "A6XX");
            rnn_control = rnn_new(!options.color);
            if (has_a7xx_gen3_control_regs()) {
               rnn_load_file(rnn_control, "adreno/adreno_control_regs.xml",
                             "A7XX_GEN3_CONTROL_REG");
            } else {
               rnn_load_file(rnn_control, "adreno/adreno_control_regs.xml",
                             "A7XX_CONTROL_REG");
            }
            rnn_pipe = rnn_new(!options.color);
            rnn_load_file(rnn_pipe, "adreno/adreno_pipe_regs.xml",
                          "A7XX_PIPE_REG");
         } else if (is_a6xx()) {
            rnn_gmu = rnn_new(!options.color);
            rnn_load_file(rnn_gmu, "adreno/a6xx_gmu.xml", "A6XX");
            rnn_control = rnn_new(!options.color);
            rnn_load_file(rnn_control, "adreno/adreno_control_regs.xml",
                          "A6XX_CONTROL_REG");
            rnn_pipe = rnn_new(!options.color);
            rnn_load_file(rnn_pipe, "adreno/adreno_pipe_regs.xml",
                          "A6XX_PIPE_REG");
         } else if (is_a5xx()) {
            rnn_control = rnn_new(!options.color);
            rnn_load_file(rnn_control, "adreno/adreno_control_regs.xml",
                          "A5XX_CONTROL_REG");
         } else {
            rnn_control = NULL;
         }

         snapshot_write_header(options.dev_id.chip_id);
      } else if (startswith(line, "fault-info:")) {
         decode_fault_info();
      } else if (startswith(line, "bos:")) {
         decode_bos();
      } else if (startswith(line, "ringbuffer:")) {
         decode_ringbuffer();
      } else if (startswith(line, "gmu-log:")) {
         decode_gmu_log();
      } else if (startswith(line, "gmu-hfi:")) {
         decode_gmu_hfi();
      } else if (startswith(line, "registers:")) {
         decode_registers();
      } else if (startswith(line, "registers-gmu:")) {
         decode_gmu_registers();
      } else if (startswith(line, "indexed-registers:")) {
         decode_indexed_registers();
      } else if (startswith(line, "shader-blocks:")) {
         decode_shader_blocks();
      } else if (startswith(line, "clusters:")) {
         decode_clusters();
      } else if (startswith(line, "debugbus:")) {
         decode_debugbus();
      }
   }
}

static void
decode_finalize(void)
{
   snapshot_linux.ptbase = ptbase;

   /* Dump cmdstream at the end after we know we've decoded all sections that might
    * contain reg vals needed for locating the cmdstream:
    */
   dump_cmdstream();

   /* If we are exporting snapshot, finalize it now: */
   do_snapshot();
}

/*
 * Usage and argument parsing:
 */

static void
usage(void)
{
   /* clang-format off */
   fprintf(stderr, "Usage:\n\n"
           "\tcrashdec [-achmsv] [-f FILE] [-S FILE]\n\n"
           "Options:\n"
           "\t-a, --allregs   - show all registers (including ones not written since\n"
           "\t                  previous draw) at each draw\n"
           "\t-c, --color     - use colors\n"
           "\t-f, --file=FILE - read input from specified file (rather than stdin)\n"
           "\t-h, --help      - this usage message\n"
           "\t-m, --markers   - try to decode CP_NOP string markers\n"
           "\t-S, --snapshot  - export crashdump to snapshot format\n"
           "\t-s, --summary   - don't show individual register writes, but just show\n"
           "\t                  register values on draws\n"
           "\t-v, --verbose   - dump more verbose output, including contents of\n"
           "\t                  less interesting buffers\n"
           "\n"
   );
   /* clang-format on */
   exit(2);
}

/* clang-format off */
static const struct option opts[] = {
      { .name = "allregs", .has_arg = 0, NULL, 'a' },
      { .name = "color",   .has_arg = 0, NULL, 'c' },
      { .name = "file",    .has_arg = 1, NULL, 'f' },
      { .name = "help",    .has_arg = 0, NULL, 'h' },
      { .name = "markers", .has_arg = 0, NULL, 'm' },
      { .name = "snapshot",.has_arg = 1, NULL, 'S' },
      { .name = "summary", .has_arg = 0, NULL, 's' },
      { .name = "verbose", .has_arg = 0, NULL, 'v' },
      {}
};
/* clang-format on */

static bool interactive;

static void
cleanup(void)
{
   fflush(stdout);

   if (interactive) {
      pager_close();
   }

   cffdec_finish();
}

int
main(int argc, char **argv)
{
   int c;

   interactive = isatty(STDOUT_FILENO);
   options.color = interactive;

   /* default to read from stdin: */
   in = stdin;

   while ((c = getopt_long(argc, argv, "acf:l:hmS:sv", opts, NULL)) != -1) {
      switch (c) {
      case 'a':
         options.allregs = true;
         break;
      case 'c':
         options.color = true;
         break;
      case 'f':
         in = fopen(optarg, "r");
         break;
      case 'l':
         lookback = atoi(optarg);
         break;
      case 'm':
         options.decode_markers = true;
         break;
      case 'S':
         snapshot = fopen(optarg, "w");
         break;
      case 's':
         options.summary = true;
         break;
      case 'v':
         verbose = true;
         break;
      case 'h':
      default:
         usage();
      }
   }

   disasm_a3xx_set_debug(PRINT_RAW);

   if (snapshot) {
      freopen("/dev/null", "w", stdout);
      verbose = false;
   } else if (interactive) {
      pager_open();
   }

   atexit(cleanup);

   decode();
   cleanup();
}
