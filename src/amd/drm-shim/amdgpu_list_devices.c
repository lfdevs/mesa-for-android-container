/* Copyright 2026 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include "common/amdgpu_devices.h"

int
main(void)
{
   for (int d = 0; d < num_amdgpu_devices; d++)
      printf("%s\n", amdgpu_devices[d].name);

   return 0;
}
