/*
 * Copyright © 2017 Rob Clark <robdclark@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util/os_file.h"

#include "compiler/isaspec/isaspec.h"

#include "freedreno_pm4.h"

#include "qrisc.h"
#include "qrisc-isa.h"
#include "util.h"
#include "emu.h"

int gpuver;

/* non-verbose mode should output something suitable to feed back into
 * assembler.. verbose mode has additional output useful for debugging
 * (like unexpected bits that are set)
 */
static bool verbose = false;

/* emulator mode: */
static bool emulator = false;

#define printerr(fmt, ...) qrisc_printc(QRISC_ERR, fmt, ##__VA_ARGS__)
#define printlbl(fmt, ...) qrisc_printc(QRISC_LBL, fmt, ##__VA_ARGS__)

static const char *
getpm4(uint32_t id)
{
   return qrisc_pm_id_name(id);
}

static void
print_gpu_reg(FILE *out, uint32_t regbase)
{
   if (regbase < 0x100)
      return;

   char *name = qrisc_gpu_reg_name(regbase);
   if (name) {
      fprintf(out, "\t; %s", name);
      free(name);
   }
}

void
print_control_reg(uint32_t id)
{
   char *name = qrisc_control_reg_name(id);
   if (name) {
      printf("@%s", name);
      free(name);
   } else {
      printf("0x%03x", id);
   }
}

void
print_sqe_reg(uint32_t id)
{
   char *name = qrisc_sqe_reg_name(id);
   if (name) {
      printf("@%s", name);
      free(name);
   } else {
      printf("0x%03x", id);
   }
}

void
print_pipe_reg(uint32_t id)
{
   char *name = qrisc_pipe_reg_name(id);
   if (name) {
      printf("|%s", name);
      free(name);
   } else {
      printf("0x%03x", id);
   }
}

struct decode_state {
   uint32_t immed;
   uint8_t shift;
   bool has_immed;
   bool dst_is_addr;
};

static void
field_print_cb(struct isa_print_state *state, const char *field_name, uint64_t val)
{
   if (!strcmp(field_name, "CONTROLREG")) {
      char *name = qrisc_control_reg_name(val);
      if (name) {
         isa_print(state, "@%s", name);
         free(name);
      } else {
         isa_print(state, "0x%03x", (unsigned)val);
      }
   } else if (!strcmp(field_name, "SQEREG")) {
      char *name = qrisc_sqe_reg_name(val);
      if (name) {
         isa_print(state, "%%%s", name);
         free(name);
      } else {
         isa_print(state, "0x%03x", (unsigned)val);
      }
   }
}

static void
pre_instr_cb(void *data, unsigned n, void *instr)
{
   struct decode_state *state = data;
   state->has_immed = state->dst_is_addr = false;
   state->shift = 0;

   if (verbose)
      printf("\t%04x: %08x  ", n, *(uint32_t *)instr);
}

static void
field_cb(void *data, const char *field_name, struct isa_decode_value *val)
{
   struct decode_state *state = data;

   if (!strcmp(field_name, "RIMMED")) {
      state->immed = val->num;
      state->has_immed = true;
   }

   if (!strcmp(field_name, "SHIFT")) {
      state->shift = val->num;
   }

   if (!strcmp(field_name, "DST")) {
      if (val->num == REG_ADDR)
         state->dst_is_addr = true;
   }
}

static void
post_instr_cb(void *data, unsigned n, void *instr)
{
   struct decode_state *state = data;

   if (state->has_immed) {
      uint32_t immed = state->immed << state->shift;
      if (state->dst_is_addr && state->shift >= 16) {
         immed &= ~0x40000; /* b18 disables auto-increment of address */
         if ((immed & 0x00ffffff) == 0) {
            printf("\t; ");
            print_pipe_reg(immed >> 24);
         }
      } else {
         print_gpu_reg(stdout, immed);
      }
   }
}

uint32_t jumptbl_offset = ~0;

/* Assume that instructions that don't match are raw data */
static void
no_match(FILE *out, const BITSET_WORD *bitset, size_t size)
{
   if (jumptbl_offset != ~0 && bitset[0] == qrisc_nop_literal(jumptbl_offset, gpuver)) {
      fprintf(out, "[#jumptbl]\n");
   } else {
      fprintf(out, "[%08x]", bitset[0]);
      print_gpu_reg(out, bitset[0]);
      fprintf(out, "\n");
   }
}

static void
get_decode_options(struct isa_decode_options *options)
{
   *options = (struct isa_decode_options) {
      .gpu_id = gpuver,
      .branch_labels = true,
      .field_cb = field_cb,
      .field_print_cb = field_print_cb,
      .pre_instr_cb = pre_instr_cb,
      .post_instr_cb = post_instr_cb,
      .no_match_cb = no_match,
   };
}

static void
disasm_instr(struct isa_decode_options *options, uint32_t *instrs, unsigned pc)
{
   qrisc_isa_disasm(&instrs[pc], 4, stdout, options);
}

static void
setup_packet_table(struct isa_decode_options *options, uint32_t preempt_instr,
                   uint32_t *jmptbl, uint32_t sizedwords)
{
   struct isa_entrypoint *entrypoints = malloc((sizedwords + 1) * sizeof(struct isa_entrypoint));

   for (unsigned i = 0; i < sizedwords; i++) {
      entrypoints[i].offset = jmptbl[i];
      unsigned n = i; // + CP_NOP;
      entrypoints[i].name = qrisc_pm_id_name(n);
      if (!entrypoints[i].name) {
         char *name;
         asprintf(&name, "UNKN%d", n);
         entrypoints[i].name = name;
      }
   }

   options->entrypoints = entrypoints;
   options->entrypoint_count = sizedwords;

   /* The actual preemption entrypoint is determined via %PREEMPT_INSTR. In
    * some cases with newer firmwares this doesn't match the value of
    * IN_PREEMPT in the jump table.
    *
    * We add a new entrypoint instead of modifying IN_PREEMPT so that we can
    * get a byte-for-byte accurate reassembly.
    */
   if (preempt_instr != ~0) {
      entrypoints[options->entrypoint_count].name = "preempt";
      entrypoints[options->entrypoint_count].offset = preempt_instr;
      options->entrypoint_count++;
   }
}

static uint32_t
find_jump_table(uint32_t *instrs, uint32_t sizedwords,
                uint32_t *jmptbl, uint32_t jmptbl_size)
{
   for (unsigned i = 0; i <= sizedwords - jmptbl_size; i++) {
      bool found = true;
      for (unsigned j = 0; j < jmptbl_size; j++) {
         if (instrs[i + j] != jmptbl[j]) {
            found = false;
            break;
         }
      }
      if (found)
         return i;
   }

   return ~0;
}

static void
disasm_section(struct emu *emu, struct isa_decode_options *options,
               enum emu_processor processor, const uint32_t *offsets,
               uint32_t size)
{
   EMU_SQE_REG(PREEMPT_INSTR);

   uint32_t offset = offsets[processor];
   emu->processor = processor;

   emu_init(emu, offsets);
   emu_run_bootstrap(emu);

   /* TODO add option to emulate LPAC SQE instead */
   if (emulator && processor == EMU_PROC_SQE) {
      /* Start from clean slate: */
      emu_fini(emu);
      emu_init(emu, offsets);

      while (true) {
         disasm_instr(options, emu->instrs + offset, emu->gpr_regs.pc);
         emu_step(emu);
      }
   }

   unsigned preempt_instr = emu_get_reg32(emu, &PREEMPT_INSTR);

   setup_packet_table(options, preempt_instr, emu->jmptbl, ARRAY_SIZE(emu->jmptbl));

   jumptbl_offset = find_jump_table(emu->instrs + offset, size, emu->jmptbl,
                                    ARRAY_SIZE(emu->jmptbl));

   qrisc_isa_disasm(emu->instrs + offset, MIN2(size, jumptbl_offset) * 4, stdout, options);

   if (jumptbl_offset != ~0) {
      if (gpuver >= 7) {
         /* The BV/LPAC microcode must be aligned to 32 bytes. On a7xx, by
          * convention the firmware aligns the jumptable preceding it instead
          * of the microcode itself, with nop instructions. Insert this
          * directive to make sure that it stays aligned when reassembling
          * even if the user modifies the BR microcode.
          */
         printf(".align 32\n");
      }
      printf("jumptbl:\n");
      printf(".jumptbl\n");
      if (jumptbl_offset + ARRAY_SIZE(emu->jmptbl) != size) {
         for (unsigned i = jumptbl_offset + ARRAY_SIZE(emu->jmptbl); i < size; i++)
            printf("[%08x]\n", emu->instrs[offset + i]);
      }
   }

   emu_fini(emu);
}

static void
disasm(struct emu *emu)
{
   uint32_t sizedwords = emu->sizedwords;

   EMU_GPU_REG(CP_SQE_INSTR_BASE);
   EMU_GPU_REG(CP_LPAC_SQE_INSTR_BASE);
   EMU_CONTROL_REG(BV_INSTR_BASE);
   EMU_CONTROL_REG(DDE_BR_INSTR_BASE);
   EMU_CONTROL_REG(LPAC_INSTR_BASE);

   emu->processor = EMU_PROC_SQE;
   emu_init(emu, NULL);

   struct isa_decode_options options;
   struct decode_state state;
   get_decode_options(&options);
   options.cbdata = &state;

#ifdef BOOTSTRAP_DEBUG
   while (true) {
      disasm_instr(&options, emu->instrs, emu->gpr_regs.pc);
      emu_step(emu);
   }
#endif

   emu_run_bootstrap(emu);

   uint32_t offsets[EMU_PROC_COUNT] = {};
   uint32_t sizes[EMU_PROC_COUNT] = {};

   /* Figure out if we have BV/LPAC SQE appended: */
   if (gpuver >= 7) {
      offsets[EMU_PROC_BV] = emu_get_reg64(emu, &BV_INSTR_BASE) -
         emu_get_reg64(emu, &CP_SQE_INSTR_BASE);
      offsets[EMU_PROC_BV] /= 4;
      offsets[EMU_PROC_LPAC] = emu_get_reg64(emu, &LPAC_INSTR_BASE) -
         emu_get_reg64(emu, &CP_SQE_INSTR_BASE);
      offsets[EMU_PROC_LPAC] /= 4;
      if (gpuver >= 8) {
         /* Note: DDE_BR and DDE_BV share the same microcode */
         offsets[EMU_PROC_DDE_BR] = emu_get_reg64(emu, &DDE_BR_INSTR_BASE) -
            emu_get_reg64(emu, &CP_SQE_INSTR_BASE);
         offsets[EMU_PROC_DDE_BR] /= 4;
      }
   } else {
      if (emu_get_reg64(emu, &CP_LPAC_SQE_INSTR_BASE)) {
         offsets[EMU_PROC_LPAC] = emu_get_reg64(emu, &CP_LPAC_SQE_INSTR_BASE) -
               emu_get_reg64(emu, &CP_SQE_INSTR_BASE);
         offsets[EMU_PROC_LPAC] /= 4;
      }
   }

   /* Determine sizes of each section: */
   uint32_t prev_offset = sizedwords;
   for (int i = EMU_PROC_COUNT - 1; i >= 0; i--) {
      if (offsets[i] || i == EMU_PROC_SQE) {
         assert(prev_offset >= offsets[i]);
         sizes[i] = prev_offset - offsets[i];
         prev_offset = offsets[i];
      }
   }

   disasm_section(emu, &options, EMU_PROC_SQE, offsets, sizes[EMU_PROC_SQE]);

   const char *section_names[EMU_PROC_COUNT] = {
      [EMU_PROC_BV] = "BV",
      [EMU_PROC_LPAC] = "LPAC",
      [EMU_PROC_DDE_BR] = "DDE",
   };

   for (unsigned i = 1; i < EMU_PROC_COUNT; i++) {
      if (!offsets[i] || !section_names[i])
         continue;

      printf("\n.section %s\n", section_names[i]);
      printf(";\n");
      printf("; %s microcode:\n", section_names[i]);
      printf(";\n");

      disasm_section(emu, &options, i, offsets, sizes[i]);
   }
}

static void
disasm_raw(uint32_t *instrs, int sizedwords)
{
   struct isa_decode_options options;
   struct decode_state state;
   get_decode_options(&options);
   options.cbdata = &state;

   qrisc_isa_disasm(instrs, sizedwords * 4, stdout, &options);
}

static void
disasm_legacy(uint32_t *buf, int sizedwords)
{
   uint32_t *instrs = buf;
   const int jmptbl_start = instrs[1] & 0xffff;
   uint32_t *jmptbl = &buf[jmptbl_start];
   int i;

   struct isa_decode_options options;
   struct decode_state state;
   get_decode_options(&options);
   options.cbdata = &state;

   /* parse jumptable: */
   setup_packet_table(&options, ~0, jmptbl, 0x80);

   /* print instructions: */
   qrisc_isa_disasm(instrs, sizedwords * 4, stdout, &options);

   /* print jumptable: */
   if (verbose) {
      printf(";;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;\n");
      printf("; JUMP TABLE\n");
      for (i = 0; i < 0x7f; i++) {
         int n = i; // + CP_NOP;
         uint32_t offset = jmptbl[i];
         const char *name = getpm4(n);
         printf("%3d %02x: ", n, n);
         printf("%04x", offset);
         if (name) {
            printf("   ; %s", name);
         } else {
            printf("   ; UNKN%d", n);
         }
         printf("\n");
      }
   }
}

static void
usage(void)
{
   fprintf(stderr, "Usage:\n"
                   "\tdisasm [-g GPUVER] [-v] [-c] [-r] filename.asm\n"
                   "\t\t-c - use colors\n"
                   "\t\t-e - emulator mode\n"
                   "\t\t-g - override GPU firmware id\n"
                   "\t\t-r - raw disasm, don't try to find jumptable\n"
                   "\t\t-v - verbose output\n"
           );
   exit(2);
}

int
main(int argc, char **argv)
{
   uint32_t *buf;
   char *file;
   bool colors = false;
   size_t sz;
   int c, ret;
   bool unit_test = false;
   bool raw = false;
   enum qrisc_fwid fw_id = 0;

   /* Argument parsing: */
   while ((c = getopt(argc, argv, "ceg:rvu")) != -1) {
      switch (c) {
      case 'c':
         colors = true;
         break;
      case 'e':
         emulator = true;
         verbose  = true;
         break;
      case 'g':
         fw_id = strtol(optarg, NULL, 16);
         break;
      case 'r':
         raw = true;
         break;
      case 'v':
         verbose = true;
         break;
      case 'u':
         /* special "hidden" flag for unit tests, to avoid file paths (which
          * can differ from reference output)
          */
         unit_test = true;
         break;
      default:
         usage();
      }
   }

   if (optind >= argc) {
      fprintf(stderr, "no file specified!\n");
      usage();
   }

   file = argv[optind];

   buf = (uint32_t *)os_read_file(file, &sz);

   if (!fw_id)
      fw_id = qrisc_get_fwid(buf[1]);

   ret = qrisc_util_init(fw_id, &gpuver, colors);
   if (ret < 0) {
      usage();
   }

   /* a6xx is *mostly* a superset of a5xx, but some opcodes shuffle
    * around, and behavior of special regs is a bit different.  Right
    * now we only bother to support the a6xx variant.
    */
   if (emulator && (gpuver < 6 || gpuver > 7)) {
      fprintf(stderr, "Emulator only supported on a6xx-a7xx!\n");
      return 1;
   }

   printf("; a%dxx microcode\n", gpuver);

   if (!unit_test)
      printf("; Disassembling microcode: %s\n", file);
   printf("; Version: %08x\n\n", buf[1]);

   if (raw) {
      disasm_raw(buf, sz / 4);
   } else if (gpuver < 6) {
      disasm_legacy(&buf[1], sz / 4 - 1);
   } else {
      struct emu emu = {
            .instrs = &buf[1],
            .sizedwords = sz / 4 - 1,
            .fw_id = fw_id,
      };

      disasm(&emu);
   }

   return 0;
}
