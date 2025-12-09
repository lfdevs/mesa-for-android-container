/*
 * Copyright © 2022 Mary Guillemard
 * SPDX-License-Identifier: MIT
 */
#include "mme_sim.h"

#include "mme_fermi.h"
#include "mme_fermi_sim.h"
#include "mme_tu104.h"
#include "mme_tu104_sim.h"

#define MME_CLS_FERMI 0x9000
#define MME_CLS_TURING 0xc500

void
mme_sim_core(const struct nv_device_info *devinfo,
             size_t macro_size, const void *macro,
             const struct mme_sim_state_ops *state_ops,
             void *state_handler)
{
   if (devinfo->cls_eng3d >= MME_CLS_TURING) {
      assert(macro_size % 12 == 0);
      uint32_t inst_count = macro_size / 12;
      struct mme_tu104_inst *insts =
         malloc(inst_count * sizeof(struct mme_tu104_inst));
      mme_tu104_decode(insts, macro, inst_count);
      mme_tu104_sim_core(inst_count, insts, state_ops, state_handler);
      free(insts);
   } else if (devinfo->cls_eng3d >= MME_CLS_FERMI) {
      assert(macro_size % 4 == 0);
      uint32_t inst_count = macro_size / 4;
      struct mme_fermi_inst *insts =
         malloc(inst_count * sizeof(struct mme_fermi_inst));
      mme_fermi_decode(insts, macro, inst_count);
      mme_fermi_sim_core(inst_count, insts, state_ops, state_handler);
      free(insts);
   } else {
      UNREACHABLE("Unsupported GPU class");
   }
}
