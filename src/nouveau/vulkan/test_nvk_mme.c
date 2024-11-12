/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_mme.h"

#include "nv_push_cla097.h"
#include "nv_push_cla0c0.h"
#include "nv_push_clc397.h"
#include "nv_push_clc3c0.h"
#include "nv_push_clc597.h"
#include "nv_push_clc5c0.h"

int main(int argc, char **argv)
{
//   static const struct nv_device_info kepler = {
//      .cls_eng3d = KEPLER_A,
//      .cls_compute = KEPLER_COMPUTE_A,
//   };
   static const struct nv_device_info volta = {
      .cls_eng3d = VOLTA_A,
      .cls_compute = VOLTA_COMPUTE_A,
   };
   static const struct nv_device_info turing = {
      .cls_eng3d = TURING_A,
      .cls_compute = TURING_COMPUTE_A,
   };

//   nvk_test_build_all_mmes(&kepler);
   nvk_test_all_mmes(&volta);
   nvk_test_all_mmes(&turing);

   return 0;
}
