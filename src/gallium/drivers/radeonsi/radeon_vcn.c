/*
 * Copyright © 2022 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "radeon_vcn.h"

/* vcn unified queue (sq) ib header */
void rvcn_sq_header(struct radeon_cmdbuf *cs,
                    struct rvcn_sq_var *sq,
                    bool enc)
{
   /* vcn ib engine info */
   radeon_emit(cs, RADEON_VCN_ENGINE_INFO_SIZE);
   radeon_emit(cs, RADEON_VCN_ENGINE_INFO);
   radeon_emit(cs, enc ? RADEON_VCN_ENGINE_TYPE_ENCODE
                       : RADEON_VCN_ENGINE_TYPE_DECODE);
   sq->engine_ib_size_of_packages = &cs->current.buf[cs->current.cdw];
   radeon_emit(cs, 0);
}

void rvcn_sq_tail(struct radeon_cmdbuf *cs,
                  struct rvcn_sq_var *sq)
{
   uint32_t *end;
   uint32_t size_in_dw;

   end = &cs->current.buf[cs->current.cdw];

   if (sq->engine_ib_size_of_packages == NULL)
      return;

   size_in_dw = end - sq->engine_ib_size_of_packages + 3;
   *sq->engine_ib_size_of_packages = size_in_dw * sizeof(uint32_t);
}
