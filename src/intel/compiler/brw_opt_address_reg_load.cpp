/*
 * Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_fs.h"
#include "brw_builder.h"
#include "brw_cfg.h"
#include "brw_eu.h"

/** @file brw_fs_opt_address_reg_load.cpp
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

using namespace brw;

static bool
opt_address_reg_load_local(fs_visitor &s, bblock_t *block, const brw::def_analysis &defs)
{
   bool progress = false;

   foreach_inst_in_block_safe(fs_inst, inst, block) {
      if (!inst->dst.is_address() || inst->opcode != BRW_OPCODE_MOV)
         continue;

      fs_inst *src_inst = defs.get(inst->src[0]);
      if (src_inst == NULL)
         continue;

      if (src_inst->uses_address_register_implicitly() ||
          src_inst->sources > 2)
         continue;

      brw_builder ubld = brw_builder(&s).at(block, inst).exec_all().group(1, 0);
      brw_reg sources[3];
      for (unsigned i = 0; i < src_inst->sources; i++) {
         sources[i] = inst->src[i].file == VGRF ? component(src_inst->src[i], 0) : src_inst->src[i];
      }
      ubld.emit(src_inst->opcode, inst->dst, sources, src_inst->sources);

      inst->remove(block);

      progress = true;
   }

   return progress;
}

bool
brw_opt_address_reg_load(fs_visitor &s)
{
   bool progress = false;
   const brw::def_analysis &defs = s.def_analysis.require();

   foreach_block(block, s.cfg) {
      foreach_inst_in_block_safe(fs_inst, inst, block) {
         progress = opt_address_reg_load_local(s, block, defs) || progress;
      }
   }

   if (progress) {
      s.cfg->adjust_block_ips();
      s.invalidate_analysis(DEPENDENCY_INSTRUCTIONS);
   }

   return progress;
}
