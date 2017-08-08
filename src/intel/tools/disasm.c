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

#include "compiler/brw_inst.h"
#include "compiler/brw_eu.h"

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

static int
gen_disasm_find_end(struct gen_disasm *disasm, void *assembly, int start)
{
   struct gen_device_info *devinfo = &disasm->devinfo;
   int offset = start;

   /* This loop exits when send-with-EOT or when opcode is 0 */
   while (true) {
      brw_inst *insn = assembly + offset;

      if (brw_inst_cmpt_control(devinfo, insn)) {
         offset += 8;
      } else {
         offset += 16;
      }

      /* Simplistic, but efficient way to terminate disasm */
      uint32_t opcode = brw_inst_opcode(devinfo, insn);
      if (opcode == 0 || (is_send(opcode) && brw_inst_eot(devinfo, insn))) {
         break;
      }
   }

   return offset;
}

void
gen_disasm_disassemble(struct gen_disasm *disasm, void *assembly,
                       int start, FILE *out)
{
   struct gen_device_info *devinfo = &disasm->devinfo;
   int end = gen_disasm_find_end(disasm, assembly, start);

   /* Make a dummy annotation structure that brw_validate_instructions
    * can work from.
    */
   struct annotation_info annotation_info = {
      .ann_count = 1,
      .ann_size = 2,
   };
   annotation_info.mem_ctx = ralloc_context(NULL);
   annotation_info.ann = rzalloc_array(annotation_info.mem_ctx,
                                       struct annotation,
                                       annotation_info.ann_size);
   annotation_info.ann[0].offset = start;
   annotation_info.ann[1].offset = end;
   brw_validate_instructions(devinfo, assembly, start, end, &annotation_info);
   struct annotation *annotation = annotation_info.ann;

   for (int i = 0; i < annotation_info.ann_count; i++) {
      int start_offset = annotation[i].offset;
      int end_offset = annotation[i + 1].offset;

      brw_disassemble(devinfo, assembly, start_offset, end_offset, stdout);

      if (annotation[i].error) {
         fputs(annotation[i].error, stdout);
      }
   }

   ralloc_free(annotation_info.mem_ctx);
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
