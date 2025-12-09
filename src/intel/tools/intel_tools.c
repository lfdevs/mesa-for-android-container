/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "intel_tools.h"

#include "compiler/brw/brw_disasm.h"
#include "compiler/brw/brw_isa_info.h"
#ifdef INTEL_USE_ELK
#include "compiler/elk/elk_disasm.h"
#include "compiler/elk/elk_isa_info.h"
#endif
#include "dev/intel_device_info.h"

static void
not_supported(const struct intel_device_info *devinfo)
{
   fprintf(stderr, "ERROR: Tool compiled without support for Gfx version %d.\n",
           devinfo->ver);
   exit(EXIT_FAILURE);
}

void
intel_disassemble(const struct intel_device_info *devinfo,
                  const void *assembly, int start, FILE *out)
{
      if (devinfo->ver >= 9) {
         struct brw_isa_info isa;
         brw_init_isa_info(&isa, devinfo);
         brw_disassemble_with_errors(&isa, assembly, start, NULL, out);
      } else {
#ifdef INTEL_USE_ELK
         struct elk_isa_info isa;
         elk_init_isa_info(&isa, devinfo);
         elk_disassemble_with_errors(&isa, assembly, start, out);
#else
         not_supported(devinfo);
#endif
      }
}

void
intel_decoder_init(struct intel_batch_decode_ctx *ctx,
                   struct intel_isa_info *isa_info,
                   const struct intel_device_info *devinfo,
                   FILE *fp, enum intel_batch_decode_flags flags,
                   const char *xml_path,
                   struct intel_batch_decode_bo (*get_bo)(void *, bool, uint64_t),
                   unsigned (*get_state_size)(void *, uint64_t, uint64_t),
                   void *user_data)
{
   if (devinfo->ver >= 9) {
      struct brw_isa_info *isa = &isa_info->brw_isa;
      brw_init_isa_info(isa, devinfo);
      intel_batch_decode_ctx_init_brw(ctx, isa, devinfo, fp,
                                      flags, xml_path, get_bo, get_state_size, user_data);
   } else {
#ifdef INTEL_USE_ELK
      struct elk_isa_info *isa = &isa_info->elk_isa;
      elk_init_isa_info(isa, devinfo);
      intel_batch_decode_ctx_init_elk(ctx, isa, devinfo, fp,
                                      flags, xml_path, get_bo, get_state_size, user_data);
#else
      not_supported(devinfo);
#endif
   }
}
