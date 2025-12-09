/*
 * Copyright 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#include "brw_shader.h"
#include "brw_builder.h"

static brw_reg
build_ex_desc(const brw_builder &bld, unsigned reg_size, bool unspill)
{
   /* Use a different area of the address register than what is used in
    * brw_lower_logical_sends.c (brw_address_reg(2)) so we don't have
    * interactions between the spill/fill instructions and the other send
    * messages.
    */
   brw_reg ex_desc = bld.vaddr(BRW_TYPE_UD,
                               BRW_ADDRESS_SUBREG_INDIRECT_SPILL_DESC);

   brw_builder ubld = bld.uniform();

   ubld.AND(ex_desc,
            retype(brw_vec1_grf(0, 5), BRW_TYPE_UD),
            brw_imm_ud(INTEL_MASK(31, 10)));

   const intel_device_info *devinfo = bld.shader->devinfo;
   if (devinfo->verx10 >= 200) {
      ubld.SHR(ex_desc, ex_desc, brw_imm_ud(4));
   } else {
      if (unspill) {
         ubld.OR(ex_desc, ex_desc, brw_imm_ud(BRW_SFID_UGM));
      } else {
         ubld.OR(ex_desc,
                 ex_desc,
                 brw_imm_ud(brw_message_ex_desc(devinfo, reg_size) | BRW_SFID_UGM));
      }
   }

   return ex_desc;
}

static void
brw_lower_lsc_fill(const intel_device_info *devinfo, brw_shader &s,
                   brw_inst *inst)
{
   assert(devinfo->verx10 >= 125);

   const brw_builder bld(inst);
   brw_reg dst = inst->dst;
   brw_reg offset = inst->src[FILL_SRC_PAYLOAD1];

   const unsigned reg_size = inst->dst.component_size(inst->exec_size) /
                             REG_SIZE;
   brw_reg ex_desc = build_ex_desc(bld, reg_size, true);

   /* LSC is limited to SIMD16 (SIMD32 on Xe2) load/store but we can
    * load more using transpose messages.
    */
   const bool use_transpose = inst->as_scratch()->use_transpose;
   const brw_builder ubld = use_transpose ? bld.uniform() : bld;

   uint32_t desc = lsc_msg_desc(devinfo, LSC_OP_LOAD,
                                LSC_ADDR_SURFTYPE_SS,
                                LSC_ADDR_SIZE_A32,
                                LSC_DATA_SIZE_D32,
                                use_transpose ? reg_size * 8 : 1 /* num_channels */,
                                use_transpose,
                                LSC_CACHE(devinfo, LOAD, L1STATE_L3MOCS));


   brw_send_inst *unspill_inst = ubld.SEND();
   unspill_inst->dst = dst;

   unspill_inst->src[SEND_SRC_DESC] = brw_imm_ud(0);
   unspill_inst->src[SEND_SRC_EX_DESC] = ex_desc;
   unspill_inst->src[SEND_SRC_PAYLOAD1] = offset;
   unspill_inst->src[SEND_SRC_PAYLOAD2] = brw_reg();

   unspill_inst->sfid = BRW_SFID_UGM;
   unspill_inst->header_size = 0;
   unspill_inst->mlen = lsc_msg_addr_len(devinfo, LSC_ADDR_SIZE_A32,
                                         unspill_inst->exec_size);
   unspill_inst->ex_mlen = 0;
   unspill_inst->size_written =
      lsc_msg_dest_len(devinfo, LSC_DATA_SIZE_D32, bld.dispatch_width()) * REG_SIZE;
   unspill_inst->has_side_effects = false;
   unspill_inst->is_volatile = true;

   unspill_inst->src[0] = brw_imm_ud(
      desc |
      brw_message_desc(devinfo,
                       unspill_inst->mlen,
                       unspill_inst->size_written / REG_SIZE,
                       unspill_inst->header_size));

   assert(unspill_inst->size_written == inst->size_written);
   assert(unspill_inst->size_read(devinfo, SEND_SRC_PAYLOAD1) == inst->size_read(devinfo, FILL_SRC_PAYLOAD1));

   inst->remove();
}

static void
brw_lower_lsc_spill(const intel_device_info *devinfo, brw_inst *inst)
{
   assert(devinfo->verx10 >= 125);

   const brw_builder bld(inst);
   brw_reg offset = inst->src[SPILL_SRC_PAYLOAD1];
   brw_reg src = inst->src[SPILL_SRC_PAYLOAD2];

   const unsigned reg_size = src.component_size(bld.dispatch_width()) /
                             REG_SIZE;

   assert(!inst->as_scratch()->use_transpose);

   const brw_reg ex_desc = build_ex_desc(bld, reg_size, false);

   brw_send_inst *spill_inst = bld.SEND();

   spill_inst->src[SEND_SRC_DESC]     = brw_imm_ud(0);
   spill_inst->src[SEND_SRC_EX_DESC]  = ex_desc;
   spill_inst->src[SEND_SRC_PAYLOAD1] = offset;
   spill_inst->src[SEND_SRC_PAYLOAD2] = src;

   spill_inst->sfid = BRW_SFID_UGM;
   uint32_t desc = lsc_msg_desc(devinfo, LSC_OP_STORE,
                                LSC_ADDR_SURFTYPE_SS,
                                LSC_ADDR_SIZE_A32,
                                LSC_DATA_SIZE_D32,
                                1 /* num_channels */,
                                false /* transpose */,
                                LSC_CACHE(devinfo, LOAD, L1STATE_L3MOCS));
   spill_inst->header_size = 0;
   spill_inst->mlen = lsc_msg_addr_len(devinfo, LSC_ADDR_SIZE_A32,
                                       bld.dispatch_width());
   spill_inst->ex_mlen = reg_size;
   spill_inst->size_written = 0;
   spill_inst->has_side_effects = true;
   spill_inst->is_volatile = false;

   spill_inst->src[0] = brw_imm_ud(
      desc |
      brw_message_desc(devinfo,
                       spill_inst->mlen,
                       spill_inst->size_written / REG_SIZE,
                       spill_inst->header_size));

   assert(spill_inst->size_written == inst->size_written);
   assert(spill_inst->size_read(devinfo, SEND_SRC_PAYLOAD1) == inst->size_read(devinfo, SPILL_SRC_PAYLOAD1));
   assert(spill_inst->size_read(devinfo, SEND_SRC_PAYLOAD2) == inst->size_read(devinfo, SPILL_SRC_PAYLOAD2));

   inst->remove();
}

bool
brw_lower_fill_and_spill(brw_shader &s)
{
   bool progress = false;

   foreach_block_and_inst_safe(block, brw_inst, inst, s.cfg) {
      switch (inst->opcode) {
      case SHADER_OPCODE_LSC_FILL:
         brw_lower_lsc_fill(s.devinfo, s, inst);
         progress = true;
         break;

      case SHADER_OPCODE_LSC_SPILL:
         brw_lower_lsc_spill(s.devinfo, inst);
         progress = true;
         break;

      default:
         break;
      }
   }

   if (progress)
      s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS |
                            BRW_DEPENDENCY_VARIABLES);

   return progress;
}
