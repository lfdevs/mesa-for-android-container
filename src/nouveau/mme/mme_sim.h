/*
 * Copyright © 2022 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */
#ifndef MME_SIM_H
#define MME_SIM_H

#include "nv_device_info.h"

#ifdef __cplusplus
extern "C" {
#endif

struct mme_sim_state_ops {
   uint32_t (*load)(void *handler);
   uint32_t (*state)(void *handler, uint16_t addr);
   void (*mthd)(void *handler, uint16_t addr, uint32_t data);
   void (*barrier)(void *handler);
   uint32_t *(*map_dram)(void *handler, uint32_t idx);
};

void mme_sim_core(const struct nv_device_info *devinfo,
                  size_t macro_size, const void *macro,
                  const struct mme_sim_state_ops *state_ops,
                  void *state_handler);

#ifdef __cplusplus
}
#endif

#endif /* MME_SIM_H */
