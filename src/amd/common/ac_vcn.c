/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_vcn.h"
#include "ac_cmdbuf.h"

void
ac_vcn_sq_header(struct ac_cmdbuf *cs, struct rvcn_sq_var *sq, unsigned type)
{
   ac_cmdbuf_begin(cs);
   ac_cmdbuf_emit(RADEON_VCN_ENGINE_INFO_SIZE);
   ac_cmdbuf_emit(RADEON_VCN_ENGINE_INFO);
   ac_cmdbuf_emit(type);
   ac_cmdbuf_emit(0);
   ac_cmdbuf_end();

   sq->engine_ib_size_of_packages = &cs->buf[cs->cdw - 1];
}

void
ac_vcn_sq_tail(struct ac_cmdbuf *cs, struct rvcn_sq_var *sq)
{
   uint32_t *end = &cs->buf[cs->cdw];
   uint32_t size_in_dw = end - sq->engine_ib_size_of_packages + 3;

   assert(cs->cdw <= cs->max_dw);
   *sq->engine_ib_size_of_packages = size_in_dw * sizeof(uint32_t);
}
