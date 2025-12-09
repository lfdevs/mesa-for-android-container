/*
 * Copyright © 2018 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_asm.h"
#include "brw_asm_internal.h"
#include "brw_disasm_info.h"
#include "util/hash_table.h"
#include "util/u_dynarray.h"

/* TODO: Check if we can use bison/flex without globals. */

extern FILE *yyin;

struct brw_codegen *p;
const char *input_filename;
int errors;
bool compaction_warning_given;

/*
 * Label tracking.
 */
static struct hash_table *brw_asm_labels;

typedef struct {
   char *name;
   int offset; /* -1 for unset */
   struct util_dynarray jip_uses;
   struct util_dynarray uip_uses;
} brw_asm_label;

static brw_asm_label *
brw_asm_label_lookup(const char *name)
{
   uint32_t h = _mesa_hash_string(name);
   struct hash_entry *entry =
      _mesa_hash_table_search_pre_hashed(brw_asm_labels, h, name);
   if (!entry) {
      void *mem_ctx = brw_asm_labels;
      brw_asm_label *label = rzalloc(mem_ctx, brw_asm_label);
      label->name = ralloc_strdup(mem_ctx, name);
      label->offset = -1;
      util_dynarray_init(&label->jip_uses, mem_ctx);
      util_dynarray_init(&label->uip_uses, mem_ctx);
      entry = _mesa_hash_table_insert_pre_hashed(brw_asm_labels,
                                                 h, name, label);
   }
   assert(entry);
   return entry->data;
}

void
brw_asm_label_set(const char *name)
{
   brw_asm_label *label = brw_asm_label_lookup(name);
   label->offset = p->next_insn_offset;
}

void
brw_asm_label_use_jip(const char *name)
{
   brw_asm_label *label = brw_asm_label_lookup(name);
   int offset = p->next_insn_offset - sizeof(brw_eu_inst);
   util_dynarray_append(&label->jip_uses, offset);
   /* Will be patched later. */
   brw_eu_inst_set_jip(p->devinfo, brw_last_inst, 0);
}

void
brw_asm_label_use_uip(const char *name)
{
   brw_asm_label *label = brw_asm_label_lookup(name);
   int offset = p->next_insn_offset - sizeof(brw_eu_inst);
   util_dynarray_append(&label->uip_uses, offset);
   /* Will be patched later. */
   brw_eu_inst_set_uip(p->devinfo, brw_last_inst, 0);
}

static bool
brw_postprocess_labels()
{
   unsigned unknown = 0;
   void *store = p->store;

   hash_table_foreach(brw_asm_labels, entry) {
      brw_asm_label *label = entry->data;

      if (label->offset == -1) {
         fprintf(stderr, "Unknown label '%s'\n", label->name);
         unknown++;
         continue;
      }

      util_dynarray_foreach(&label->jip_uses, int, use_offset) {
         brw_eu_inst *inst = store + *use_offset;
         brw_eu_inst_set_jip(p->devinfo, inst, label->offset - *use_offset);
      }

      util_dynarray_foreach(&label->uip_uses, int, use_offset) {
         brw_eu_inst *inst = store + *use_offset;
         brw_eu_inst_set_uip(p->devinfo, inst, label->offset - *use_offset);
      }
   }

   return unknown == 0;
}

/* TODO: Would be nice to make this operate on string instead on a FILE. */

brw_assemble_result
brw_assemble(void *mem_ctx, const struct intel_device_info *devinfo,
             FILE *f, const char *filename, brw_assemble_flags flags)
{
   brw_assemble_result result = {0};

   void *tmp_ctx = ralloc_context(mem_ctx);
   brw_asm_labels = _mesa_string_hash_table_create(tmp_ctx);

   struct brw_isa_info isa;
   brw_init_isa_info(&isa, devinfo);

   p = rzalloc(mem_ctx, struct brw_codegen);
   brw_init_codegen(&isa, p, p);

   yyin = f;
   input_filename = filename;

   compaction_warning_given = false;
   int err = yyparse();
   if (err || errors)
      goto end;

   if (!brw_postprocess_labels())
      goto end;

   struct disasm_info *disasm_info = disasm_initialize(p->isa, NULL);
   if (!disasm_info) {
      ralloc_free(disasm_info);
      fprintf(stderr, "Unable to initialize disasm_info struct instance\n");
      goto end;
   }

   /* Add "inst groups" so validation errors can be recorded. */
   for (int i = 0; i <= p->next_insn_offset; i += 16)
      disasm_new_inst_group(disasm_info, i);

   if (!brw_validate_instructions(p->isa, p->store, 0,
                                  p->next_insn_offset, disasm_info)) {
      dump_assembly(p->store, 0, p->next_insn_offset, disasm_info, NULL, stderr);
      ralloc_free(disasm_info);
      fprintf(stderr, "Invalid instructions.\n");
      goto end;
   }

   if ((flags & BRW_ASSEMBLE_COMPACT) != 0)
      brw_compact_instructions(p, 0, disasm_info);

   result.bin = p->store;
   result.bin_size = p->next_insn_offset;

   if ((flags & BRW_ASSEMBLE_DUMP) != 0)
      dump_assembly(p->store, 0, p->next_insn_offset, disasm_info, NULL, stderr);

   ralloc_free(disasm_info);

end:
   /* Reset internal state. */
   yyin = NULL;
   input_filename = NULL;
   p = NULL;
   brw_asm_labels = NULL;

   ralloc_free(tmp_ctx);

   return result;
}

