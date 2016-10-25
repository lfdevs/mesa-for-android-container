/*
 * Copyright © 2014 Intel Corporation
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

#include <stdlib.h>

#include "brw_context.h"
#include "brw_inst.h"
#include "brw_eu.h"

#include "gen_disasm.h"

uint64_t INTEL_DEBUG;

struct gen_disasm {
    struct gen_device_info devinfo;
};

static bool
is_send(uint32_t opcode)
{
   return (opcode == BRW_OPCODE_SEND  ||
           opcode == BRW_OPCODE_SENDC ||
           opcode == BRW_OPCODE_SENDS ||
           opcode == BRW_OPCODE_SENDSC );
}

void
gen_disasm_disassemble(struct gen_disasm *disasm, void *assembly,
                       int start, FILE *out)
{
   struct gen_device_info *devinfo = &disasm->devinfo;
   bool dump_hex = false;
   int offset = start;

   /* This loop exits when send-with-EOT or when opcode is 0 */
   while (true) {
      brw_inst *insn = assembly + offset;
      brw_inst uncompacted;
      bool compacted = brw_inst_cmpt_control(devinfo, insn);
      if (0)
         fprintf(out, "0x%08x: ", offset);

      if (compacted) {
         brw_compact_inst *compacted = (void *)insn;
         if (dump_hex) {
            fprintf(out, "0x%08x 0x%08x                       ",
                   ((uint32_t *)insn)[1],
                   ((uint32_t *)insn)[0]);
         }

         brw_uncompact_instruction(devinfo, &uncompacted, compacted);
         insn = &uncompacted;
         offset += 8;
      } else {
         if (dump_hex) {
            fprintf(out, "0x%08x 0x%08x 0x%08x 0x%08x ",
                   ((uint32_t *)insn)[3],
                   ((uint32_t *)insn)[2],
                   ((uint32_t *)insn)[1],
                   ((uint32_t *)insn)[0]);
         }
         offset += 16;
      }

      brw_disassemble_inst(out, devinfo, insn, compacted);

      /* Simplistic, but efficient way to terminate disasm */
      uint32_t opcode = brw_inst_opcode(devinfo, insn);
      if (opcode == 0 || (is_send(opcode) && brw_inst_eot(devinfo, insn))) {
         break;
      }
   }
}

struct gen_disasm *
gen_disasm_create(int pciid)
{
   struct gen_disasm *gd;

   gd = malloc(sizeof *gd);
   if (gd == NULL)
      return NULL;

   if (!gen_get_device_info(pciid, &gd->devinfo)) {
      free(gd);
      return NULL;
   }

   brw_init_compaction_tables(&gd->devinfo);

   return gd;
}

void
gen_disasm_destroy(struct gen_disasm *disasm)
{
   free(disasm);
}
