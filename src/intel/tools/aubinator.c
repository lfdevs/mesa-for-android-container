/*
 * Copyright © 2016 Intel Corporation
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <getopt.h>

#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mman.h>

#include "util/macros.h"

#include "common/gen_decoder.h"
#include "intel_aub.h"
#include "gen_disasm.h"

/* Below is the only command missing from intel_aub.h in libdrm
 * So, reuse intel_aub.h from libdrm and #define the
 * AUB_MI_BATCH_BUFFER_END as below
 */
#define AUB_MI_BATCH_BUFFER_END (0x0500 << 16)

#define CSI "\e["
#define BLUE_HEADER  CSI "0;44m"
#define GREEN_HEADER CSI "1;42m"
#define NORMAL       CSI "0m"

/* options */

static bool option_full_decode = true;
static bool option_print_offsets = true;
static enum { COLOR_AUTO, COLOR_ALWAYS, COLOR_NEVER } option_color;

/* state */

uint16_t pci_id = 0;
char *input_file = NULL, *xml_path = NULL;
struct gen_device_info devinfo;
struct gen_batch_decode_ctx batch_ctx;

uint64_t gtt_size, gtt_end;
void *gtt;
uint64_t general_state_base;
uint64_t surface_state_base;
uint64_t dynamic_state_base;
uint64_t instruction_base;
uint64_t instruction_bound;

FILE *outfile;

static inline uint32_t
field(uint32_t value, int start, int end)
{
   uint32_t mask;

   mask = ~0U >> (31 - end + start);

   return (value >> start) & mask;
}

struct brw_instruction;

static inline int
valid_offset(uint32_t offset)
{
   return offset < gtt_end;
}

#define GEN_ENGINE_RENDER 1
#define GEN_ENGINE_BLITTER 2

static void
handle_trace_block(uint32_t *p)
{
   int operation = p[1] & AUB_TRACE_OPERATION_MASK;
   int type = p[1] & AUB_TRACE_TYPE_MASK;
   int address_space = p[1] & AUB_TRACE_ADDRESS_SPACE_MASK;
   uint64_t offset = p[3];
   uint32_t size = p[4];
   int header_length = p[0] & 0xffff;
   uint32_t *data = p + header_length + 2;
   int engine = GEN_ENGINE_RENDER;

   if (devinfo.gen >= 8)
      offset += (uint64_t) p[5] << 32;

   switch (operation) {
   case AUB_TRACE_OP_DATA_WRITE:
      if (address_space != AUB_TRACE_MEMTYPE_GTT)
         break;
      if (gtt_size < offset + size) {
         fprintf(stderr, "overflow gtt space: %s\n", strerror(errno));
         exit(EXIT_FAILURE);
      }
      memcpy((char *) gtt + offset, data, size);
      if (gtt_end < offset + size)
         gtt_end = offset + size;
      break;
   case AUB_TRACE_OP_COMMAND_WRITE:
      switch (type) {
      case AUB_TRACE_TYPE_RING_PRB0:
         engine = GEN_ENGINE_RENDER;
         break;
      case AUB_TRACE_TYPE_RING_PRB2:
         engine = GEN_ENGINE_BLITTER;
         break;
      default:
         fprintf(outfile, "command write to unknown ring %d\n", type);
         break;
      }

      (void)engine; /* TODO */
      gen_print_batch(&batch_ctx, data, size, 0);

      gtt_end = 0;
      break;
   }
}

static struct gen_batch_decode_bo
get_gen_batch_bo(void *user_data, uint64_t address)
{
   if (address > gtt_end)
      return (struct gen_batch_decode_bo) { .map = NULL };

   /* We really only have one giant address range */
   return (struct gen_batch_decode_bo) {
      .addr = 0,
      .map = gtt,
      .size = gtt_size
   };
}

static void
aubinator_init(uint16_t aub_pci_id, const char *app_name)
{
   if (!gen_get_device_info(pci_id, &devinfo)) {
      fprintf(stderr, "can't find device information: pci_id=0x%x\n", pci_id);
      exit(EXIT_FAILURE);
   }

   enum gen_batch_decode_flags batch_flags = 0;
   if (option_color == COLOR_ALWAYS)
      batch_flags |= GEN_BATCH_DECODE_IN_COLOR;
   if (option_full_decode)
      batch_flags |= GEN_BATCH_DECODE_FULL;
   if (option_print_offsets)
      batch_flags |= GEN_BATCH_DECODE_OFFSETS;
   batch_flags |= GEN_BATCH_DECODE_FLOATS;

   gen_batch_decode_ctx_init(&batch_ctx, &devinfo, outfile, batch_flags,
                             xml_path, get_gen_batch_bo, NULL);

   char *color = GREEN_HEADER, *reset_color = NORMAL;
   if (option_color == COLOR_NEVER)
      color = reset_color = "";

   fprintf(outfile, "%sAubinator: Intel AUB file decoder.%-80s%s\n",
           color, "", reset_color);

   if (input_file)
      fprintf(outfile, "File name:        %s\n", input_file);

   if (aub_pci_id)
      fprintf(outfile, "PCI ID:           0x%x\n", aub_pci_id);

   fprintf(outfile, "Application name: %s\n", app_name);

   fprintf(outfile, "Decoding as:      %s\n", gen_get_device_name(pci_id));

   /* Throw in a new line before the first batch */
   fprintf(outfile, "\n");
}

static void
handle_trace_header(uint32_t *p)
{
   /* The intel_aubdump tool from IGT is kind enough to put a PCI-ID= tag in
    * the AUB header comment.  If the user hasn't specified a hardware
    * generation, try to use the one from the AUB file.
    */
   uint32_t *end = p + (p[0] & 0xffff) + 2;
   int aub_pci_id = 0;
   if (end > &p[12] && p[12] > 0)
      sscanf((char *)&p[13], "PCI-ID=%i", &aub_pci_id);

   if (pci_id == 0)
      pci_id = aub_pci_id;

   char app_name[33];
   strncpy(app_name, (char *)&p[2], 32);
   app_name[32] = 0;

   aubinator_init(aub_pci_id, app_name);
}

static void
handle_memtrace_version(uint32_t *p)
{
   int header_length = p[0] & 0xffff;
   char app_name[64];
   int app_name_len = MIN2(4 * (header_length + 1 - 5), ARRAY_SIZE(app_name) - 1);
   int pci_id_len = 0;
   int aub_pci_id = 0;

   strncpy(app_name, (char *)&p[5], app_name_len);
   app_name[app_name_len] = 0;
   sscanf(app_name, "PCI-ID=%i %n", &aub_pci_id, &pci_id_len);
   if (pci_id == 0)
      pci_id = aub_pci_id;
   aubinator_init(aub_pci_id, app_name + pci_id_len);
}

static void
handle_memtrace_reg_write(uint32_t *p)
{
   uint32_t offset = p[1];
   uint32_t value = p[5];
   int engine;
   static int render_elsp_writes = 0;
   static int blitter_elsp_writes = 0;
   static int render_elsq0 = 0;
   static int blitter_elsq0 = 0;
   uint8_t *pphwsp;

   if (offset == 0x2230) {
      render_elsp_writes++;
      engine = GEN_ENGINE_RENDER;
   } else if (offset == 0x22230) {
      blitter_elsp_writes++;
      engine = GEN_ENGINE_BLITTER;
   } else if (offset == 0x2510) {
      render_elsq0 = value;
   } else if (offset == 0x22510) {
      blitter_elsq0 = value;
   } else if (offset == 0x2550 || offset == 0x22550) {
      /* nothing */;
   } else {
      return;
   }

   if (render_elsp_writes > 3 || blitter_elsp_writes > 3) {
      render_elsp_writes = blitter_elsp_writes = 0;
      pphwsp = (uint8_t*)gtt + (value & 0xfffff000);
   } else if (offset == 0x2550) {
      engine = GEN_ENGINE_RENDER;
      pphwsp = (uint8_t*)gtt + (render_elsq0 & 0xfffff000);
   } else if (offset == 0x22550) {
      engine = GEN_ENGINE_BLITTER;
      pphwsp = (uint8_t*)gtt + (blitter_elsq0 & 0xfffff000);
   } else {
      return;
   }

   const uint32_t pphwsp_size = 4096;
   uint32_t *context = (uint32_t*)(pphwsp + pphwsp_size);
   uint32_t ring_buffer_head = context[5];
   uint32_t ring_buffer_tail = context[7];
   uint32_t ring_buffer_start = context[9];
   uint32_t *commands = (uint32_t*)((uint8_t*)gtt + ring_buffer_start + ring_buffer_head);
   (void)engine; /* TODO */
   gen_print_batch(&batch_ctx, commands, ring_buffer_tail - ring_buffer_head, 0);
}

static void
handle_memtrace_mem_write(uint32_t *p)
{
   uint64_t address = *(uint64_t*)&p[1];
   uint32_t address_space = p[3] >> 28;
   uint32_t size = p[4];
   uint32_t *data = p + 5;

   if (address_space != 1)
      return;

   if (gtt_size < address + size) {
      fprintf(stderr, "overflow gtt space: %s\n", strerror(errno));
      exit(EXIT_FAILURE);
   }

   memcpy((char *) gtt + address, data, size);
   if (gtt_end < address + size)
      gtt_end = address + size;
}

struct aub_file {
   FILE *stream;

   uint32_t *map, *end, *cursor;
   uint32_t *mem_end;
};

static struct aub_file *
aub_file_open(const char *filename)
{
   struct aub_file *file;
   struct stat sb;
   int fd;

   file = calloc(1, sizeof *file);
   fd = open(filename, O_RDONLY);
   if (fd == -1) {
      fprintf(stderr, "open %s failed: %s\n", filename, strerror(errno));
      exit(EXIT_FAILURE);
   }

   if (fstat(fd, &sb) == -1) {
      fprintf(stderr, "stat failed: %s\n", strerror(errno));
      exit(EXIT_FAILURE);
   }

   file->map = mmap(NULL, sb.st_size,
                    PROT_READ, MAP_SHARED, fd, 0);
   if (file->map == MAP_FAILED) {
      fprintf(stderr, "mmap failed: %s\n", strerror(errno));
      exit(EXIT_FAILURE);
   }

   close(fd);

   file->cursor = file->map;
   file->end = file->map + sb.st_size / 4;

   return file;
}

static struct aub_file *
aub_file_stdin(void)
{
   struct aub_file *file;

   file = calloc(1, sizeof *file);
   file->stream = stdin;

   return file;
}

#define TYPE(dw)       (((dw) >> 29) & 7)
#define OPCODE(dw)     (((dw) >> 23) & 0x3f)
#define SUBOPCODE(dw)  (((dw) >> 16) & 0x7f)

#define MAKE_HEADER(type, opcode, subopcode) \
                   (((type) << 29) | ((opcode) << 23) | ((subopcode) << 16))

#define TYPE_AUB            0x7

/* Classic AUB opcodes */
#define OPCODE_AUB          0x01
#define SUBOPCODE_HEADER    0x05
#define SUBOPCODE_BLOCK     0x41
#define SUBOPCODE_BMP       0x1e

/* Newer version AUB opcode */
#define OPCODE_NEW_AUB      0x2e
#define SUBOPCODE_REG_POLL  0x02
#define SUBOPCODE_REG_WRITE 0x03
#define SUBOPCODE_MEM_POLL  0x05
#define SUBOPCODE_MEM_WRITE 0x06
#define SUBOPCODE_VERSION   0x0e

#define MAKE_GEN(major, minor) ( ((major) << 8) | (minor) )

enum {
   AUB_ITEM_DECODE_OK,
   AUB_ITEM_DECODE_FAILED,
   AUB_ITEM_DECODE_NEED_MORE_DATA,
};

static int
aub_file_decode_batch(struct aub_file *file)
{
   uint32_t *p, h, *new_cursor;
   int header_length, bias;

   if (file->end - file->cursor < 1)
      return AUB_ITEM_DECODE_NEED_MORE_DATA;

   p = file->cursor;
   h = *p;
   header_length = h & 0xffff;

   switch (OPCODE(h)) {
   case OPCODE_AUB:
      bias = 2;
      break;
   case OPCODE_NEW_AUB:
      bias = 1;
      break;
   default:
      fprintf(outfile, "unknown opcode %d at %td/%td\n",
              OPCODE(h), file->cursor - file->map,
              file->end - file->map);
      return AUB_ITEM_DECODE_FAILED;
   }

   new_cursor = p + header_length + bias;
   if ((h & 0xffff0000) == MAKE_HEADER(TYPE_AUB, OPCODE_AUB, SUBOPCODE_BLOCK)) {
      if (file->end - file->cursor < 4)
         return AUB_ITEM_DECODE_NEED_MORE_DATA;
      new_cursor += p[4] / 4;
   }

   if (new_cursor > file->end)
      return AUB_ITEM_DECODE_NEED_MORE_DATA;

   switch (h & 0xffff0000) {
   case MAKE_HEADER(TYPE_AUB, OPCODE_AUB, SUBOPCODE_HEADER):
      handle_trace_header(p);
      break;
   case MAKE_HEADER(TYPE_AUB, OPCODE_AUB, SUBOPCODE_BLOCK):
      handle_trace_block(p);
      break;
   case MAKE_HEADER(TYPE_AUB, OPCODE_AUB, SUBOPCODE_BMP):
      break;
   case MAKE_HEADER(TYPE_AUB, OPCODE_NEW_AUB, SUBOPCODE_VERSION):
      handle_memtrace_version(p);
      break;
   case MAKE_HEADER(TYPE_AUB, OPCODE_NEW_AUB, SUBOPCODE_REG_WRITE):
      handle_memtrace_reg_write(p);
      break;
   case MAKE_HEADER(TYPE_AUB, OPCODE_NEW_AUB, SUBOPCODE_MEM_WRITE):
      handle_memtrace_mem_write(p);
      break;
   case MAKE_HEADER(TYPE_AUB, OPCODE_NEW_AUB, SUBOPCODE_MEM_POLL):
      fprintf(outfile, "memory poll block (dwords %d):\n", h & 0xffff);
      break;
   case MAKE_HEADER(TYPE_AUB, OPCODE_NEW_AUB, SUBOPCODE_REG_POLL):
      break;
   default:
      fprintf(outfile, "unknown block type=0x%x, opcode=0x%x, "
             "subopcode=0x%x (%08x)\n", TYPE(h), OPCODE(h), SUBOPCODE(h), h);
      break;
   }
   file->cursor = new_cursor;

   return AUB_ITEM_DECODE_OK;
}

static int
aub_file_more_stuff(struct aub_file *file)
{
   return file->cursor < file->end || (file->stream && !feof(file->stream));
}

#define AUB_READ_BUFFER_SIZE (4096)
#define MAX(a, b) ((a) < (b) ? (b) : (a))

static void
aub_file_data_grow(struct aub_file *file)
{
   size_t old_size = (file->mem_end - file->map) * 4;
   size_t new_size = MAX(old_size * 2, AUB_READ_BUFFER_SIZE);
   uint32_t *new_start = realloc(file->map, new_size);

   file->cursor = new_start + (file->cursor - file->map);
   file->end = new_start + (file->end - file->map);
   file->map = new_start;
   file->mem_end = file->map + (new_size / 4);
}

static bool
aub_file_data_load(struct aub_file *file)
{
   size_t r;

   if (file->stream == NULL)
      return false;

   /* First remove any consumed data */
   if (file->cursor > file->map) {
      memmove(file->map, file->cursor,
              (file->end - file->cursor) * 4);
      file->end -= file->cursor - file->map;
      file->cursor = file->map;
   }

   /* Then load some new data in */
   if ((file->mem_end - file->end) < (AUB_READ_BUFFER_SIZE / 4))
      aub_file_data_grow(file);

   r = fread(file->end, 1, (file->mem_end - file->end) * 4, file->stream);
   file->end += r / 4;

   return r != 0;
}

static void
setup_pager(void)
{
   int fds[2];
   pid_t pid;

   if (!isatty(1))
      return;

   if (pipe(fds) == -1)
      return;

   pid = fork();
   if (pid == -1)
      return;

   if (pid == 0) {
      close(fds[1]);
      dup2(fds[0], 0);
      execlp("less", "less", "-FRSi", NULL);
   }

   close(fds[0]);
   dup2(fds[1], 1);
   close(fds[1]);
}

static void
print_help(const char *progname, FILE *file)
{
   fprintf(file,
           "Usage: %s [OPTION]... [FILE]\n"
           "Decode aub file contents from either FILE or the standard input.\n\n"
           "A valid --gen option must be provided.\n\n"
           "      --help          display this help and exit\n"
           "      --gen=platform  decode for given platform (3 letter platform name)\n"
           "      --headers       decode only command headers\n"
           "      --color[=WHEN]  colorize the output; WHEN can be 'auto' (default\n"
           "                        if omitted), 'always', or 'never'\n"
           "      --no-pager      don't launch pager\n"
           "      --no-offsets    don't print instruction offsets\n"
           "      --xml=DIR       load hardware xml description from directory DIR\n",
           progname);
}

int main(int argc, char *argv[])
{
   struct aub_file *file;
   int c, i;
   bool help = false, pager = true;
   const struct option aubinator_opts[] = {
      { "help",       no_argument,       (int *) &help,                 true },
      { "no-pager",   no_argument,       (int *) &pager,                false },
      { "no-offsets", no_argument,       (int *) &option_print_offsets, false },
      { "gen",        required_argument, NULL,                          'g' },
      { "headers",    no_argument,       (int *) &option_full_decode,   false },
      { "color",      required_argument, NULL,                          'c' },
      { "xml",        required_argument, NULL,                          'x' },
      { NULL,         0,                 NULL,                          0 }
   };

   outfile = stdout;

   i = 0;
   while ((c = getopt_long(argc, argv, "", aubinator_opts, &i)) != -1) {
      switch (c) {
      case 'g': {
         const int id = gen_device_name_to_pci_device_id(optarg);
         if (id < 0) {
            fprintf(stderr, "can't parse gen: '%s', expected ivb, byt, hsw, "
                                   "bdw, chv, skl, kbl or bxt\n", optarg);
            exit(EXIT_FAILURE);
         } else {
            pci_id = id;
         }
         break;
      }
      case 'c':
         if (optarg == NULL || strcmp(optarg, "always") == 0)
            option_color = COLOR_ALWAYS;
         else if (strcmp(optarg, "never") == 0)
            option_color = COLOR_NEVER;
         else if (strcmp(optarg, "auto") == 0)
            option_color = COLOR_AUTO;
         else {
            fprintf(stderr, "invalid value for --color: %s", optarg);
            exit(EXIT_FAILURE);
         }
         break;
      case 'x':
         xml_path = strdup(optarg);
         break;
      default:
         break;
      }
   }

   if (help || argc == 1) {
      print_help(argv[0], stderr);
      exit(0);
   }

   if (optind < argc)
      input_file = argv[optind];

   /* Do this before we redirect stdout to pager. */
   if (option_color == COLOR_AUTO)
      option_color = isatty(1) ? COLOR_ALWAYS : COLOR_NEVER;

   if (isatty(1) && pager)
      setup_pager();

   if (input_file == NULL)
      file = aub_file_stdin();
   else
      file = aub_file_open(input_file);

   /* mmap a terabyte for our gtt space. */
   gtt_size = 1ull << 40;
   gtt = mmap(NULL, gtt_size, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS |  MAP_NORESERVE, -1, 0);
   if (gtt == MAP_FAILED) {
      fprintf(stderr, "failed to alloc gtt space: %s\n", strerror(errno));
      exit(EXIT_FAILURE);
   }

   while (aub_file_more_stuff(file)) {
      switch (aub_file_decode_batch(file)) {
      case AUB_ITEM_DECODE_OK:
         break;
      case AUB_ITEM_DECODE_NEED_MORE_DATA:
         if (!file->stream) {
            file->cursor = file->end;
            break;
         }
         if (aub_file_more_stuff(file) && !aub_file_data_load(file)) {
            fprintf(stderr, "failed to load data from stdin\n");
            exit(EXIT_FAILURE);
         }
         break;
      default:
         fprintf(stderr, "failed to parse aubdump data\n");
         exit(EXIT_FAILURE);
      }
   }


   fflush(stdout);
   /* close the stdout which is opened to write the output */
   close(1);
   free(xml_path);

   wait(NULL);

   return EXIT_SUCCESS;
}
