/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_cmdbuf.h"
#include "ac_cmdbuf_video.h"
#include "ac_vcn.h"
#include "ac_vcn_dec.h"

static void *
vcn_common_cmd(struct ac_cmdbuf *cs, uint32_t type, uint32_t size)
{
   struct rvcn_sq_var sq;

   ac_vcn_sq_header(cs, &sq, RADEON_VCN_ENGINE_TYPE_COMMON);
   struct rvcn_cmn_engine_ib_package *ib_header = (struct rvcn_cmn_engine_ib_package *)&(cs->buf[cs->cdw]);
   ib_header->package_size = sizeof(struct rvcn_cmn_engine_ib_package) + size;
   cs->cdw++;
   ib_header->package_type = type;
   cs->cdw++;

   void *ret = &(cs->buf[cs->cdw]);
   cs->cdw += size / 4;
   ac_vcn_sq_tail(cs, &sq);

   return ret;
}

void
ac_emit_video_write_memory(struct ac_cmdbuf *cs, const struct radeon_info *info,
                           enum amd_ip_type ip_type, uint64_t va, uint64_t value)
{
   if (ip_type == AMD_IP_VCN_DEC) {
      struct ac_vcn_dec_reg reg;
      ac_vcn_dec_init_regs(&reg, info->vcn_ip_version);
      if (reg.data2) {
         ac_cmdbuf_begin(cs);
         ac_cmdbuf_emit(RDECODE_PKT0(reg.data0 >> 2, 0));
         ac_cmdbuf_emit(va);
         ac_cmdbuf_emit(RDECODE_PKT0(reg.data1 >> 2, 0));
         ac_cmdbuf_emit(va >> 32);
         ac_cmdbuf_emit(RDECODE_PKT0(reg.data2 >> 2, 0));
         ac_cmdbuf_emit(value);
         ac_cmdbuf_emit(RDECODE_PKT0(reg.cmd >> 2, 0));
         ac_cmdbuf_emit(RDECODE_CMD_WRITE_MEMORY << 1);
         ac_cmdbuf_end();
      }
   } else if (ip_type == AMD_IP_VCN_ENC) {
      struct rvcn_cmn_engine_op_writememory *write_memory =
         vcn_common_cmd(cs, RADEON_VCN_IB_COMMON_OP_WRITEMEMORY, sizeof(struct rvcn_cmn_engine_op_writememory));
      write_memory->dest_addr_lo = va;
      write_memory->dest_addr_hi = va >> 32;
      write_memory->data = value;
   }
}

void
ac_emit_video_write_timestamp(struct ac_cmdbuf *cs, enum amd_ip_type ip_type, uint64_t va)
{
   if (ip_type == AMD_IP_VCN_ENC) {
      struct rvcn_cmn_engine_op_timestamp *timestamp =
         vcn_common_cmd(cs, RADEON_VCN_IB_COMMON_OP_TIMESTAMP, sizeof(struct rvcn_cmn_engine_op_timestamp));
      timestamp->timestamp_addr_lo = va & 0xffffffff;
      timestamp->timestamp_addr_hi = va >> 32;
   }
}
