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

#include "util/macros.h"
#include "util/log.h"
#include "qrisc.h"
#include "asm.h"
#include "parser.h"
#include "util.h"

struct encode_state {
	unsigned gen;
};

static qrisc_opc
__instruction_case(struct encode_state *s, const struct qrisc_instr *instr)
{
   switch (instr->opc) {
#define ALU(name) \
   case OPC_##name: \
      if (instr->has_immed || instr->label.ref1.str) \
         return OPC_##name##I; \
      break;

   ALU(ADD)
   ALU(ADDHI)
   ALU(SUB)
   ALU(SUBHI)
   ALU(AND)
   ALU(OR)
   ALU(XOR)
   ALU(NOT)
   ALU(SHL)
   ALU(USHR)
   ALU(ISHR)
   ALU(ROT)
   ALU(MUL8)
   ALU(MIN)
   ALU(MAX)
   ALU(CMP)
   ALU(BIC)
#undef ALU

   default:
      break;
   }

   return instr->opc;
}

#include "encode.h"

int gpuver;

/* bit lame to hard-code max but fw sizes are small */
static struct qrisc_instr instructions[0x8000];
static unsigned num_instructions;

static unsigned instr_offset;
static unsigned section_offset;

static struct asm_label labels[0x1000];
static unsigned num_labels;

static unsigned section_offsets[4];
static unsigned num_sections;

const char *cur_section;

static int outfd;

struct qrisc_instr *
next_instr(qrisc_opc opc)
{
   struct qrisc_instr *ai = &instructions[num_instructions++];
   assert(num_instructions < ARRAY_SIZE(instructions));
   memset(ai, 0, sizeof(*ai));
   instr_offset++;
   ai->opc = opc;
   return ai;
}

static void usage(void);

void
parse_version(struct qrisc_instr *instr)
{
   if (gpuver != 0)
      return;

   int ret = qrisc_util_init(qrisc_get_fwid(instr->literal), &gpuver, false);
   if (ret < 0) {
      usage();
      exit(1);
   }
}

void
decl_label(const char *str)
{
   struct asm_label *label = &labels[num_labels++];

   assert(num_labels < ARRAY_SIZE(labels));

   label->abs_offset = instr_offset;
   label->offset = instr_offset - section_offset;
   label->label = str;
   label->section = cur_section;
}

void
decl_jumptbl(void)
{
   struct qrisc_instr *ai = &instructions[num_instructions++];
   assert(num_instructions < ARRAY_SIZE(instructions));
   ai->opc = OPC_JUMPTBL;
   ai->label.ref1.section = (char *)cur_section;
   instr_offset += 0x80;
}

void
align_instr(unsigned alignment)
{
   while (instr_offset % (alignment / 4) != 0) {
      next_instr(OPC_NOP);
   }
}

static int
resolve_label_ref(struct qrisc_label_ref label_ref)
{
   int i;

   for (i = 0; i < num_labels; i++) {
      struct asm_label *label = &labels[i];

      if (label_ref.section && !label->section)
         continue;
      if (label->section && !label_ref.section)
         continue;
      if (label_ref.section && strcmp(label->section, label_ref.section))
         continue;

      if (!strcmp(label_ref.str, label->label))
         return label_ref.absolute ? label->abs_offset : label->offset;
   }

   if (label_ref.section) {
      fprintf(stderr, "Undeclared label: %s#%s\n", label_ref.str,
              label_ref.section);
   } else {
      fprintf(stderr, "Undeclared label: %s\n", label_ref.str);
   }
   exit(2);
}

static int
resolve_label(struct qrisc_label_expr label)
{
   int val = resolve_label_ref(label.ref1) * label.ref1_scale;
   if (label.ref2.str) {
      int val2 = resolve_label_ref(label.ref2) * label.ref2_scale;
      switch (label.op) {
      case LABEL_OP_ADD:
         val += val2;
         break;
      case LABEL_OP_SUB:
         val -= val2;
         break;
      default:
         UNREACHABLE("unknown label op");
      }
   }
   return val;
}

static void
emit_jumptable(int outfd, const char *section)
{
   uint32_t jmptable[0x80] = {0};
   int i;

   for (i = 0; i < num_labels; i++) {
      struct asm_label *label = &labels[i];
      if (section && !label->section)
         continue;
      if (label->section && !section)
         continue;
      if (section && strcmp(label->section, section))
         continue;

      int id = qrisc_pm4_id(label->label);

      /* if it doesn't match a known PM4 packet-id, try to match UNKN%d: */
      if (id < 0) {
         if (sscanf(label->label, "UNKN%d", &id) != 1) {
            /* if still not found, must not belong in jump-table: */
            continue;
         }
      }

      jmptable[id] = label->offset;
   }

   write(outfd, jmptable, sizeof(jmptable));
}

static void
emit_instructions(int outfd)
{
   int i;

   struct encode_state s = {
      .gen = gpuver,
   };

   unsigned next_section = 0;
   unsigned cur_section_offset = 0;
   unsigned abs_instr_offset = 0;

   /* Expand some meta opcodes, and resolve branch targets */
   for (i = 0; i < num_instructions; i++) {
      if (next_section < num_sections &&
          abs_instr_offset >= section_offsets[next_section]) {
         assert(abs_instr_offset == section_offsets[next_section]);
         cur_section_offset = abs_instr_offset;
         next_section++;
      }

      int instr_offset = abs_instr_offset - cur_section_offset;

      struct qrisc_instr *ai = &instructions[i];

      switch (ai->opc) {
      case OPC_BREQ:
         ai->offset = resolve_label(ai->label) - instr_offset;
         if (ai->has_bit)
            ai->opc = OPC_BREQB;
         else
            ai->opc = OPC_BREQI;
         break;

      case OPC_BRNE:
         ai->offset = resolve_label(ai->label) - instr_offset;
         if (ai->has_bit)
            ai->opc = OPC_BRNEB;
         else
            ai->opc = OPC_BRNEI;
         break;

      case OPC_JUMP:
         ai->offset = resolve_label(ai->label) - instr_offset;
         ai->opc = OPC_BRNEB;
         ai->src1 = 0;
         ai->bit = 0;
         break;

      case OPC_CALL:
      case OPC_BL:
      case OPC_JUMPA:
         ai->literal = resolve_label(ai->label);
         break;

      case OPC_MOVI:
      case OPC_ADD:
      case OPC_ADDHI:
      case OPC_SUB:
      case OPC_SUBHI:
      case OPC_AND:
      case OPC_OR:
      case OPC_XOR:
      case OPC_SHL:
      case OPC_USHR:
      case OPC_ROT:
      case OPC_MUL8:
      case OPC_MUL16:
      case OPC_MIN:
      case OPC_MAX:
      case OPC_CMP:
      case OPC_BIC:
         if (ai->label.ref1.str)
            ai->immed = resolve_label(ai->label);
         break;

      default:
         break;
      }

      if (ai->opc == OPC_JUMPTBL) {
         emit_jumptable(outfd, ai->label.ref1.section);
         abs_instr_offset += 0x80;
         continue;
      }

      if (ai->opc == OPC_RAW_LITERAL) {
         if (ai->label.ref1.str) {
            ai->literal = qrisc_nop_literal(resolve_label(ai->label), gpuver);
         }
         write(outfd, &ai->literal, 4);
         abs_instr_offset++;
         continue;
      }

      uint32_t encoded = bitmask_to_uint64_t(encode__instruction(&s, NULL, ai));
      write(outfd, &encoded, 4);
      abs_instr_offset++;
   }
}

void next_section(const char *section)
{
   /* Sections must be aligned to 32 bytes */
   align_instr(32);

   section_offset = instr_offset;
   cur_section = section;
   assert(num_sections < ARRAY_SIZE(section_offsets));
   section_offsets[num_sections++] = section_offset;
}


unsigned
parse_control_reg(const char *name)
{
   /* skip leading "@" */
   return qrisc_control_reg(name + 1);
}

unsigned
parse_sqe_reg(const char *name)
{
   /* skip leading "%" */
   return qrisc_sqe_reg(name + 1);
}

static void
usage(void)
{
   fprintf(stderr, "Usage:\n"
                   "\tasm filename.asm filename.fw\n");
   exit(2);
}

int
main(int argc, char **argv)
{
   FILE *in;
   char *file, *outfile;
   int ret;

   if (optind >= (argc + 1)) {
      fprintf(stderr, "no file specified!\n");
      usage();
   }

   file = argv[optind];
   outfile = argv[optind + 1];

   outfd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
   if (outfd < 0) {
      fprintf(stderr, "could not open \"%s\"\n", outfile);
      usage();
   }

   in = fopen(file, "r");
   if (!in) {
      fprintf(stderr, "could not open \"%s\"\n", file);
      usage();
   }

   yyset_in(in);

   /* there is an extra 0x00000000 which kernel strips off.. we could
    * perhaps use it for versioning.
    */
   uint32_t zero = 0;
   write(outfd, &zero, 4);

   ret = yyparse();
   if (ret) {
      fprintf(stderr, "parse failed: %d\n", ret);
      return ret;
   }

   emit_instructions(outfd);

   close(outfd);

   return 0;
}
