/*
 * Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_shader.h"
#include "brw_builder.h"
#include "brw_cfg.h"
#include "brw_eu.h"

/** @file
 *
 * Turn this sequence :
 *
 *    add(8) vgrf64:UD, vgrf63:UD,        192u
 *    mov(1)   a0.4:UD, vgrf64+0.0<0>:UD
 *
 * into :
 *
 *    add(1)   a0.4:UD, vgrf63+0.0<0>:UD, 192u
 */

static bool
opt_address_reg_load_local(brw_shader &s, bblock_t *block, const brw_def_analysis &defs)
{
   bool progress = false;

   foreach_inst_in_block_safe(brw_inst, inst, block) {
      if (!inst->dst.is_address() || inst->opcode != BRW_OPCODE_MOV)
         continue;

      brw_inst *src_inst = defs.get(inst->src[0]);
      if (src_inst == NULL)
         continue;

      if (src_inst->uses_address_register_implicitly() ||
          src_inst->sources > 2)
         continue;

      brw_builder ubld = brw_builder(&s).before(inst).uniform();
      brw_reg sources[3];
      for (unsigned i = 0; i < src_inst->sources; i++) {
         const brw_reg src = src_inst->src[i];
         sources[i] = src.file == VGRF ? component(src, 0) : src;
      }
      ubld.emit(src_inst->opcode, inst->dst, sources, src_inst->sources);

      inst->remove();

      progress = true;
   }

   return progress;
}

bool
brw_opt_address_reg_load(brw_shader &s)
{
   bool progress = false;
   const brw_def_analysis &defs = s.def_analysis.require();

   foreach_block(block, s.cfg) {
      progress = opt_address_reg_load_local(s, block, defs) || progress;
   }

   if (progress) {
      s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS);
   }

   return progress;
}
