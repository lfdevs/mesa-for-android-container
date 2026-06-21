/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <array>
#include <vector>
#include <stdio.h>
#include <string.h>

#include "util/ralloc.h"
#include "util/bitpack_helpers.h"
#include "util/u_math.h"

#include "gen_private.h"

/* Generated instruction information */
#include "gen_info_util.h"
#include "gen_info_pre_xe.h"
#include "gen_info_xe.h"
#include "gen_info_xe2.h"

enum {
   GEN_SYSTOLIC_DEPTH_16 = 0,
   GEN_SYSTOLIC_DEPTH_2  = 1,
   GEN_SYSTOLIC_DEPTH_4  = 2,
   GEN_SYSTOLIC_DEPTH_8  = 3,
};

static inline unsigned
encode_sdepth(unsigned d)
{
   switch (d) {
   case 2:  return GEN_SYSTOLIC_DEPTH_2;
   case 4:  return GEN_SYSTOLIC_DEPTH_4;
   case 8:  return GEN_SYSTOLIC_DEPTH_8;
   case 16: return GEN_SYSTOLIC_DEPTH_16;
   default: UNREACHABLE("Invalid systolic depth.");
   }
}

static inline unsigned
decode_sdepth(unsigned d)
{
   switch (d) {
   case GEN_SYSTOLIC_DEPTH_2:  return 2;
   case GEN_SYSTOLIC_DEPTH_4:  return 4;
   case GEN_SYSTOLIC_DEPTH_8:  return 8;
   case GEN_SYSTOLIC_DEPTH_16: return 16;
   default: UNREACHABLE("Invalid systolic depth.");
   }
}

#define ERROR(msg) ERROR_IF(true, msg)
#define ERROR_IF(cond, msg)                             \
   do {                                                 \
      if ((cond))                                       \
         report_errorf("%s", msg);                       \
   } while(0)

#define RETURN_ERROR(msg) RETURN_ERROR_IF(true, msg)
#define RETURN_ERROR_IF(cond, msg)                      \
   do {                                                 \
      if ((cond)) {                                     \
         report_errorf("%s", msg);                       \
         return;                                        \
      }                                                 \
   } while(0)

unsigned
gen_inst_num_sources(const struct intel_device_info *devinfo,
                     const gen_inst *inst)
{
   if (gen_inst_is_send(inst))
      return gen_inst_is_split_send(devinfo, inst) ? 2 : 1;

   return gen_opcode_num_srcs(inst->opcode);
}

bool
gen_has_uip(gen_opcode op)
{
   return gen_opcode_has_prop(op, GEN_OPCODE_PROP_HAS_UIP);
}

bool
gen_has_jip(gen_opcode op)
{
   return gen_opcode_has_prop(op, GEN_OPCODE_PROP_HAS_JIP);
}

bool
gen_has_branch_ctrl(gen_opcode opcode)
{
   return gen_opcode_has_prop(opcode, GEN_OPCODE_PROP_BRANCH_CTRL);
}

static bool
inst_has_type(const intel_device_info *devinfo, const gen_inst *inst,
              gen_reg_type type)
{
   if (inst->dst.file != GEN_BAD_FILE && inst->dst.type == type)
      return true;

   const unsigned num_sources = gen_inst_num_sources(devinfo, inst);
   for (unsigned i = 0; i < num_sources; i++) {
      if (inst->src[i].type == type)
         return true;
   }

   return false;
}

bool
gen_inst_is_unordered(const intel_device_info *devinfo,
                      const gen_inst *inst)
{
   return inst->opcode == GEN_OP_SEND || inst->opcode == GEN_OP_SENDC ||
          inst->opcode == GEN_OP_SENDS || inst->opcode == GEN_OP_SENDSC ||
          (devinfo->ver < 20 && inst->opcode == GEN_OP_MATH) ||
          inst->opcode == GEN_OP_DPAS ||
          (devinfo->has_64bit_float_via_math_pipe &&
           inst_has_type(devinfo, inst, GEN_TYPE_DF));
}

/* Provide some clue in the compiler error if this gets used wrongly. */
struct gen_invalid_range {};

uint32_t
gen_swsb_encode(const struct intel_device_info *devinfo,
                gen_swsb swsb, gen_opcode op)
{
   if (!swsb.mode) {
      const unsigned pipe = devinfo->verx10 < 125 ? 0 :
         swsb.pipe == GEN_PIPE_FLOAT ? 0x10 :
         swsb.pipe == GEN_PIPE_INT ? 0x18 :
         swsb.pipe == GEN_PIPE_LONG ? 0x20 :
         swsb.pipe == GEN_PIPE_MATH ? 0x28 :
         swsb.pipe == GEN_PIPE_SCALAR ? 0x30 :
         swsb.pipe == GEN_PIPE_ALL ? 0x8 : 0;
      return pipe | swsb.regdist;

   } else if (swsb.regdist) {
      if (devinfo->ver >= 20) {
         unsigned mode = 0;
         if (op == GEN_OP_DPAS) {
            mode = (swsb.mode & GEN_SBID_SET) ? 0b01 :
                   (swsb.mode & GEN_SBID_SRC) ? 0b10 :
                 /* swsb.mode & GEN_SBID_DST */ 0b11;
         } else if (swsb.mode & GEN_SBID_SET) {
            assert(op == GEN_OP_SEND || op == GEN_OP_SENDC);
            assert(swsb.pipe == GEN_PIPE_ALL ||
                   swsb.pipe == GEN_PIPE_INT ||
                   swsb.pipe == GEN_PIPE_FLOAT);

            mode = swsb.pipe == GEN_PIPE_INT   ? 0b11 :
                   swsb.pipe == GEN_PIPE_FLOAT ? 0b10 :
                /* swsb.pipe == GEN_PIPE_ALL  */ 0b01;
         } else {
            assert(!(swsb.mode & ~(GEN_SBID_DST | GEN_SBID_SRC)));
            mode = swsb.pipe == GEN_PIPE_ALL  ? 0b11 :
                   swsb.mode == GEN_SBID_SRC  ? 0b10 :
                /* swsb.mode == GEN_SBID_DST */ 0b01;
         }
         return mode << 8 | swsb.regdist << 5 | swsb.sbid;
      } else {
         assert(!(swsb.sbid & ~0xfu));
         return 0x80 | swsb.regdist << 4 | swsb.sbid;
      }

   } else {
      if (devinfo->ver >= 20) {
         return swsb.sbid | (swsb.mode & GEN_SBID_SET ? 0xc0 :
                             swsb.mode & GEN_SBID_DST ? 0x80 : 0xa0);
      } else {
         assert(!(swsb.sbid & ~0xfu));
         return swsb.sbid | (swsb.mode & GEN_SBID_SET ? 0x40 :
                             swsb.mode & GEN_SBID_DST ? 0x20 : 0x30);
      }
   }
}

gen_lsc_desc
gen_lsc_desc_decode(const struct intel_device_info *devinfo, uint32_t raw_desc)
{
   assert(devinfo->has_lsc);

   gen_lsc_desc desc = {};

   desc.op = (enum lsc_opcode)GET_BITS(raw_desc, 5, 0);
   desc.addr_size = (enum lsc_addr_size)GET_BITS(raw_desc, 8, 7);
   desc.addr_type = (enum lsc_addr_surface_type)GET_BITS(raw_desc, 30, 29);

   if (desc.op == LSC_OP_FENCE) {
      desc.fence.scope = (enum lsc_fence_scope)GET_BITS(raw_desc, 11, 9);
      desc.fence.flush_type = (enum lsc_flush_type)GET_BITS(raw_desc, 14, 12);
      desc.fence.route_to_lsc = GET_BITS(raw_desc, 18, 18);
      return desc;
   }

   desc.data_size = (enum lsc_data_size)GET_BITS(raw_desc, 11, 9);
   desc.cache_ctrl = devinfo->ver >= 20 ? GET_BITS(raw_desc, 19, 16) :
                                          GET_BITS(raw_desc, 19, 17);

   if (lsc_opcode_has_cmask(desc.op)) {
      desc.cmask = (enum lsc_cmask)GET_BITS(raw_desc, 15, 12);
   } else {
      desc.vect_size = (enum lsc_vect_size)GET_BITS(raw_desc, 14, 12);
      desc.transpose = GET_BITS(raw_desc, 15, 15);
   }

   return desc;
}

uint32_t
gen_lsc_desc_encode(const struct intel_device_info *devinfo,
                    const gen_lsc_desc *desc)
{
   assert(devinfo->has_lsc);

   uint32_t raw_desc =
      SET_BITS(desc->op, 5, 0) |
      SET_BITS(desc->addr_size, 8, 7) |
      SET_BITS(desc->addr_type, 30, 29);

   if (desc->op == LSC_OP_FENCE) {
      raw_desc |= SET_BITS(desc->fence.scope, 11, 9) |
                  SET_BITS(desc->fence.flush_type, 14, 12) |
                  SET_BITS(desc->fence.route_to_lsc, 18, 18);
      return raw_desc;
   }

   raw_desc |= SET_BITS(desc->data_size, 11, 9) |
               (devinfo->ver >= 20 ? SET_BITS(desc->cache_ctrl, 19, 16) :
                                     SET_BITS(desc->cache_ctrl, 19, 17));

   if (lsc_opcode_has_cmask(desc->op)) {
      raw_desc |= SET_BITS(desc->cmask, 15, 12);
   } else {
      assert(!desc->transpose || lsc_opcode_has_transpose(desc->op));
      raw_desc |= SET_BITS(desc->vect_size, 14, 12) |
                  SET_BITS(desc->transpose, 15, 15);
   }

   return raw_desc;
}

gen_urb_desc
gen_urb_desc_decode(const struct intel_device_info *devinfo, uint32_t raw_desc)
{
   assert(devinfo->ver < 20);

   gen_urb_desc desc = {};
   desc.op               = (enum gen_urb_opcode)GET_BITS(raw_desc, 3, 0);
   desc.global_offset    = GET_BITS(raw_desc, 14, 4);
   desc.swizzle          = GET_BITS(raw_desc, 15, 15);
   desc.per_slot_offset  = GET_BITS(raw_desc, 17, 17);
   return desc;
}

uint32_t
gen_urb_desc_encode(const struct intel_device_info *devinfo,
                    const gen_urb_desc *desc)
{
   assert(devinfo->ver < 20);

   return SET_BITS(desc->op, 3, 0) |
          SET_BITS(desc->global_offset, 14, 4) |
          SET_BITS(desc->swizzle, 15, 15) |
          SET_BITS(desc->per_slot_offset, 17, 17);
}

gen_sampler_desc
gen_sampler_desc_decode(const struct intel_device_info *devinfo,
                        uint32_t raw_desc)
{
   gen_sampler_desc desc = {};
   desc.bti           = GET_BITS(raw_desc, 7, 0);
   desc.sampler_index = GET_BITS(raw_desc, 11, 8);
   desc.msg_type      = GET_BITS(raw_desc, 16, 12);
   if (devinfo->ver >= 20)
      desc.msg_type |= GET_BITS(raw_desc, 31, 31) << 5;
   desc.simd_mode     = GET_BITS(raw_desc, 18, 17) |
                        (GET_BITS(raw_desc, 29, 29) << 2);
   desc.return_hp     = GET_BITS(raw_desc, 30, 30);
   return desc;
}

uint32_t
gen_sampler_desc_encode(const struct intel_device_info *devinfo,
                        const gen_sampler_desc *desc)
{
   uint32_t raw_desc =
      SET_BITS(desc->bti, 7, 0) |
      SET_BITS(desc->sampler_index, 11, 8) |
      SET_BITS(desc->msg_type & 0x1fu, 16, 12) |
      SET_BITS(desc->simd_mode & 0x3u, 18, 17) |
      SET_BITS((desc->simd_mode >> 2) & 0x1u, 29, 29) |
      SET_BITS(desc->return_hp, 30, 30);
   if (devinfo->ver >= 20)
      raw_desc |= SET_BITS((desc->msg_type >> 5) & 0x1u, 31, 31);
   return raw_desc;
}

gen_hdc_desc
gen_hdc_desc_decode(const struct intel_device_info *devinfo, uint32_t raw_desc)
{
   gen_hdc_desc desc = {};
   desc.bti      = GET_BITS(raw_desc, 7, 0);
   desc.msg_ctrl = GET_BITS(raw_desc, 13, 8);
   desc.msg_type = GET_BITS(raw_desc, 18, 14);
   return desc;
}

uint32_t
gen_hdc_desc_encode(const struct intel_device_info *devinfo,
                    const gen_hdc_desc *desc)
{
   return SET_BITS(desc->bti, 7, 0) |
          SET_BITS(desc->msg_ctrl, 13, 8) |
          SET_BITS(desc->msg_type, 18, 14);
}

gen_render_desc
gen_render_desc_decode(const struct intel_device_info *devinfo,
                       uint32_t raw_desc)
{
   gen_render_desc desc = {};
   desc.bti          = GET_BITS(raw_desc, 7, 0);
   desc.msg_ctrl     = GET_BITS(raw_desc, 13, 8);
   desc.msg_type     = GET_BITS(raw_desc, 17, 14);
   desc.coarse_write = GET_BITS(raw_desc, 18, 18);
   return desc;
}

uint32_t
gen_render_desc_encode(const struct intel_device_info *devinfo,
                       const gen_render_desc *desc)
{
   return SET_BITS(desc->bti, 7, 0) |
          SET_BITS(desc->msg_ctrl, 13, 8) |
          SET_BITS(desc->msg_type, 17, 14) |
          SET_BITS(desc->coarse_write, 18, 18);
}

static uint32_t
lsc_ss_base_offset_to_imm_extra(int base_offset)
{
   const uint32_t bits17 = (uint32_t)util_bitpack_sint(base_offset, 0, 16);
   return SET_BITS(GET_BITS(bits17, 16, 4), 31, 19) |
          SET_BITS(GET_BITS(bits17,  3, 0), 15, 12);
}

static int
lsc_ss_base_offset_from_imm_extra(uint32_t imm_extra)
{
   const uint32_t bits17 = (GET_BITS(imm_extra, 31, 19) << 4) |
                           GET_BITS(imm_extra, 15, 12);
   return (int)util_sign_extend(bits17, 17);
}

gen_lsc_ex_desc
gen_lsc_ex_desc_decode(const struct intel_device_info *devinfo,
                       enum lsc_addr_surface_type addr_type,
                       uint32_t raw_ex_desc,
                       uint32_t raw_ex_desc_imm_extra)
{
   assert(devinfo->has_lsc);

   gen_lsc_ex_desc ex_desc = {};
   ex_desc.addr_type = addr_type;

   switch (addr_type) {
   case LSC_ADDR_SURFTYPE_FLAT:
      ex_desc.flat.base_offset =
         (int)util_sign_extend(GET_BITS(raw_ex_desc, 31, 12), 20);
      break;

   case LSC_ADDR_SURFTYPE_BSS:
   case LSC_ADDR_SURFTYPE_SS:
      ex_desc.surface_state.surface_state_index =
         GET_BITS(raw_ex_desc, 31, 6);
      ex_desc.surface_state.base_offset =
         lsc_ss_base_offset_from_imm_extra(raw_ex_desc_imm_extra);
      break;

   case LSC_ADDR_SURFTYPE_BTI:
      ex_desc.bti.index = GET_BITS(raw_ex_desc, 31, 24);
      ex_desc.bti.base_offset =
         (int)util_sign_extend(GET_BITS(raw_ex_desc, 23, 12), 12);
      break;

   default:
      UNREACHABLE("Invalid LSC surface address type");
   }

   return ex_desc;
}

uint32_t
gen_lsc_ex_desc_encode(const struct intel_device_info *devinfo,
                       const gen_lsc_ex_desc *ex_desc,
                       uint32_t *ex_desc_imm_extra_out)
{
   assert(devinfo->has_lsc);

   uint32_t ex_desc_imm_extra = 0;
   uint32_t result = 0;

   switch (ex_desc->addr_type) {
   case LSC_ADDR_SURFTYPE_FLAT:
      result = (uint32_t)util_bitpack_sint(ex_desc->flat.base_offset, 12, 31);
      break;

   case LSC_ADDR_SURFTYPE_BSS:
   case LSC_ADDR_SURFTYPE_SS:
      result = SET_BITS(ex_desc->surface_state.surface_state_index, 31, 6);
      ex_desc_imm_extra =
         lsc_ss_base_offset_to_imm_extra(ex_desc->surface_state.base_offset);
      break;

   case LSC_ADDR_SURFTYPE_BTI:
      result = SET_BITS(ex_desc->bti.index, 31, 24) |
               (uint32_t)util_bitpack_sint(ex_desc->bti.base_offset, 12, 23);
      break;

   default:
      UNREACHABLE("Invalid LSC surface address type");
   }

   if (ex_desc_imm_extra_out)
      *ex_desc_imm_extra_out = ex_desc_imm_extra;

   return result;
}

/**
 * Convert the provided binary representation of an SWSB annotation to a
 * gen_swsb.
 */
gen_swsb
gen_swsb_decode(const struct intel_device_info *devinfo,
                const bool is_unordered, const uint32_t x, gen_opcode op)
{
   if (devinfo->ver >= 20) {
      if (x & 0x300) {
         /* Mode isn't SingleInfo, there's a tuple */
         if (op == GEN_OP_SEND || op == GEN_OP_SENDC) {
            const gen_swsb swsb = {
               (x & 0xe0u) >> 5,
               ((x & 0x300) == 0x300 ? GEN_PIPE_INT :
                (x & 0x300) == 0x200 ? GEN_PIPE_FLOAT :
                GEN_PIPE_ALL),
               x & 0x1fu,
               GEN_SBID_SET
            };
            return swsb;
         } else if (op == GEN_OP_DPAS) {
            const gen_swsb swsb = {
               .regdist = (x & 0xe0u) >> 5,
               .pipe = GEN_PIPE_NONE,
               .sbid = x & 0x1fu,
               .mode = (x & 0x300) == 0x300 ? GEN_SBID_DST :
                       (x & 0x300) == 0x200 ? GEN_SBID_SRC :
                                              GEN_SBID_SET,
            };
            return swsb;
         } else {
            const gen_swsb swsb = {
               (x & 0xe0u) >> 5,
               ((x & 0x300) == 0x300 ? GEN_PIPE_ALL : GEN_PIPE_NONE),
               x & 0x1fu,
               ((x & 0x300) == 0x200 ? GEN_SBID_SRC : GEN_SBID_DST)
            };
            return swsb;
         }

      } else if ((x & 0xe0) == 0x80) {
         return gen_swsb_sbid(GEN_SBID_DST, x & 0x1f);
      } else if ((x & 0xe0) == 0xa0) {
         return gen_swsb_sbid(GEN_SBID_SRC, x & 0x1fu);
      } else if ((x & 0xe0) == 0xc0) {
         return gen_swsb_sbid(GEN_SBID_SET, x & 0x1fu);
      } else {
            const gen_swsb swsb = { x & 0x7u,
                                    ((x & 0x38) == 0x10 ? GEN_PIPE_FLOAT :
                                     (x & 0x38) == 0x18 ? GEN_PIPE_INT :
                                     (x & 0x38) == 0x20 ? GEN_PIPE_LONG :
                                     (x & 0x38) == 0x28 ? GEN_PIPE_MATH :
                                     (x & 0x38) == 0x30 ? GEN_PIPE_SCALAR :
                                     (x & 0x38) == 0x8 ? GEN_PIPE_ALL :
                                     GEN_PIPE_NONE) };
            return swsb;
      }

   } else {
      if (x & 0x80) {
         const struct gen_swsb swsb = { (x & 0x70u) >> 4, GEN_PIPE_NONE,
                                        x & 0xfu,
                                        is_unordered ?
                                        GEN_SBID_SET : GEN_SBID_DST };
         return swsb;
      } else if ((x & 0x70) == 0x20) {
         return gen_swsb_sbid(GEN_SBID_DST, x & 0xfu);
      } else if ((x & 0x70) == 0x30) {
         return gen_swsb_sbid(GEN_SBID_SRC, x & 0xfu);
      } else if ((x & 0x70) == 0x40) {
         return gen_swsb_sbid(GEN_SBID_SET, x & 0xfu);
      } else {
         const struct gen_swsb swsb = { x & 0x7u,
                                        ((x & 0x78) == 0x10 ? GEN_PIPE_FLOAT :
                                         (x & 0x78) == 0x18 ? GEN_PIPE_INT :
                                         (x & 0x78) == 0x50 ? GEN_PIPE_LONG :
                                         (x & 0x78) == 0x8 ? GEN_PIPE_ALL :
                                         GEN_PIPE_NONE) };
         assert(devinfo->verx10 >= 125 || swsb.pipe == GEN_PIPE_NONE);
         return swsb;
      }
   }
}


/* E is the struct with the Encoding fields and type. */
template <typename E>
struct gen_encoder {
   const intel_device_info *devinfo;

   const gen_inst *inst;
   gen_raw_inst *raw;
   const gen_inst_description *desc;

   gen_encoder(const intel_device_info *devinfo)
      : devinfo(devinfo) {}

   bool
   encode_many(gen_encode_params *params)
   {
      gen_raw_inst *raw = (gen_raw_inst *)params->raw_bytes;

      for (int i = 0; i < params->num_insts; i++) {
         encode(&params->insts[i], raw + i);
         if (params->encoded_offsets)
            params->encoded_offsets[i] = i * sizeof(gen_raw_inst);
      }

      params->raw_bytes_size = params->num_insts * sizeof(gen_raw_inst);

      if (params->errors != NULL)
         return false;

      gen_compact(params);

      return true;
   }

   void
   encode(const gen_inst *inst, gen_raw_inst *raw)
   {
      this->inst = inst;
      this->raw = raw;
      this->desc = &E::gen_to_description[inst->opcode];

      memset(raw, 0, sizeof(gen_raw_inst));

      assert(!inst->align16);

      gen_range bits = { 127, 0 };

      set(E::HW_OPCODE,      desc->hw_opcode);
      set(E::SWSB,           gen_swsb_encode(devinfo, inst->swsb, inst->opcode));
      set(E::EXEC_SIZE,      inst->exec_size ? cvt(inst->exec_size) - 1 : 0);
      set(E::FLAG_SUBNR,     inst->flag_subnr);
      set(E::FLAG_NR,        inst->flag_nr);
      set(E::PRED_CONTROL,   inst->pred_control);
      set(E::PRED_INV,       inst->pred_inv);
      set(E::DEBUG_CONTROL,  inst->debug_control);
      set(E::NO_MASK,        inst->no_mask);
      set(E::ATOMIC_CONTROL, inst->atomic_control);

      if constexpr (E::TYPE == GEN_ENCODING_XE)
         set(E::CHAN_OFFSET, inst->chan_offset / 4);
      else
         set(E::CHAN_OFFSET, inst->chan_offset / 8);

      if (gen_inst_has_saturate(desc->format, inst))
         set(E::SATURATE, inst->saturate);

      if (inst->opcode == GEN_OP_MATH) {
         set(E::MATH_FC, inst->math.func);
      } else if (inst->opcode == GEN_OP_SYNC) {
         set(E::SYNC_CTRL, inst->sync.func);
      } else if (inst->opcode == GEN_OP_BFN) {
         /* BFN supports only a few conditional modifiers. */
         const unsigned encoded_bfn_cmod =
            inst->cmod == GEN_CONDITION_ZE ? 1 :
            inst->cmod == GEN_CONDITION_GT ? 2 :
            inst->cmod == GEN_CONDITION_LT ? 3 : 0;
         set(E::BFN_COND_MODIFIER, encoded_bfn_cmod);
      } else if (gen_inst_has_cond_modifier(devinfo, desc->format, inst)) {
         set(E::COND_MODIFIER, inst->cmod);
      }

      switch (desc->format) {
      case GEN_FORMAT_BASIC_ONE_SRC:
      case GEN_FORMAT_BASIC_TWO_SRC: {
         if (desc->has_dst) {
            set(E::DST_ADDRESS_MODE, inst->dst.indirect);
            set(E::DST_TYPE,         encode_type(inst->dst.file, inst->dst.type));
            set(E::DST_HSTRIDE,      encode_hstride(inst->dst.region.hstride));

            if (inst->dst.indirect)
               encode_indirect_operand(E::DST_OPERAND, inst->dst);
            else
               encode_direct_operand(E::DST_OPERAND, inst->dst);

            if constexpr (E::TYPE >= GEN_ENCODING_XE2) {
               set(E::DST_OPERAND_EXTRA, inst->dst.indirect ? inst->dst.addr_imm & 1
                                                            : inst->dst.subnr & 1);
            }
         }

         if constexpr (E::TYPE == GEN_ENCODING_XE)
            set(E::ACC_WR_CONTROL, inst->acc_wr_control);

         set(E::SRC0_ADDRESS_MODE, inst->src[0].indirect);
         set(E::SRC0_NEGATE,       inst->src[0].negate);
         set(E::SRC0_ABS,          inst->src[0].abs);
         set(E::SRC0_TYPE,         encode_type(inst->src[0].file, inst->src[0].type));

         int imm_src = -1;

         if (inst->src[0].file == GEN_IMM) {
            imm_src = 0;
            set(E::SRC0_IS_IMM, 1);

         } else {
            if (inst->src[0].indirect)
               encode_indirect_operand(E::SRC0_OPERAND, inst->src[0]);
            else
               encode_direct_operand(E::SRC0_OPERAND, inst->src[0]);

            if constexpr (E::TYPE >= GEN_ENCODING_XE2) {
               set(E::SRC0_OPERAND_EXTRA, inst->src[0].indirect ? inst->src[0].addr_imm & 1
                                                                : inst->src[0].subnr & 1);
            }

            set(E::SRC0_VSTRIDE, encode_vstride(inst->src[0].region.vstride));
            set(E::SRC0_WIDTH,   encode_width(inst->src[0].region.width));
            set(E::SRC0_HSTRIDE, encode_hstride(inst->src[0].region.hstride));
         }

         if (desc->format == GEN_FORMAT_BASIC_TWO_SRC) {
            set(E::SRC1_ADDRESS_MODE, inst->src[1].indirect);
            set(E::SRC1_NEGATE,       inst->src[1].negate);
            set(E::SRC1_ABS,          inst->src[1].abs);
            set(E::SRC1_TYPE,         encode_type(inst->src[1].file, inst->src[1].type));

            if (inst->src[1].file == GEN_IMM) {
               assert(imm_src == -1);
               imm_src = 1;
               set(E::SRC1_IS_IMM, 1);

            } else {
               if (inst->src[1].indirect)
                  encode_indirect_operand(E::SRC1_OPERAND, inst->src[1]);
               else
                  encode_direct_operand(E::SRC1_OPERAND, inst->src[1]);

               set(E::SRC1_VSTRIDE, encode_vstride(inst->src[1].region.vstride));
               set(E::SRC1_WIDTH,   encode_width(inst->src[1].region.width));
               set(E::SRC1_HSTRIDE, encode_hstride(inst->src[1].region.hstride));
            }
         }

         if (imm_src != -1) {
            assert(inst->src[imm_src].file == GEN_IMM);

            set(E::IMM_LO_32, inst->src[imm_src].imm & 0xFFFFFFFF);
            if (gen_type_size_bytes(inst->src[imm_src].type) > 4)
               set(E::IMM_HI_32, inst->src[imm_src].imm >> 32);
         }

         break;
      }

      case GEN_FORMAT_BASIC_THREE_SRC: {
         if constexpr (E::TYPE == GEN_ENCODING_XE)
            set(E::ACC_WR_CONTROL,    inst->acc_wr_control);

         set(E::THREE_EXEC_DATA_TYPE, gen_type_is_float_or_bfloat(inst->dst.type));
         set(E::THREE_DST_TYPE,       encode_type_3src(inst->dst.type));
         set(E::THREE_SRC0_TYPE,      encode_type_3src(inst->src[0].type));
         set(E::THREE_SRC1_TYPE,      encode_type_3src(inst->src[1].type));
         set(E::THREE_SRC2_TYPE,      encode_type_3src(inst->src[2].type));

         encode_direct_operand(E::THREE_DST_OPERAND, inst->dst);
         set(E::THREE_DST_HSTRIDE,
             inst->dst.region.hstride == 1 ? GEN_3SRC_DST_HORIZONTAL_STRIDE_1
                                           : GEN_3SRC_DST_HORIZONTAL_STRIDE_2);

         if (inst->src[0].file == GEN_IMM) {
            set(E::SRC0_IS_IMM,    1);
            set(E::THREE_SRC0_IMM, inst->src[0].imm & 0xFFFF);
         } else {
            encode_direct_operand(E::THREE_SRC0_OPERAND, inst->src[0]);

            const unsigned src0_vstride = ENCODE_VSTRIDE_3SRC(inst->src[0].region.vstride);
            set(E::THREE_SRC0_VSTRIDE, src0_vstride);

            set(E::THREE_SRC0_HSTRIDE, encode_hstride(inst->src[0].region.hstride));
         }

         encode_direct_operand(E::THREE_SRC1_OPERAND, inst->src[1]);

         const unsigned src1_vstride = ENCODE_VSTRIDE_3SRC(inst->src[1].region.vstride);
         set(E::THREE_SRC1_VSTRIDE, src1_vstride);

         set(E::THREE_SRC1_HSTRIDE, encode_hstride(inst->src[1].region.hstride));

         if (inst->src[2].file == GEN_IMM) {
            set(E::THREE_SRC2_IS_IMM, 1);
            set(E::THREE_SRC2_IMM, inst->src[2].imm & 0xFFFF);
         } else {
            encode_direct_operand(E::THREE_SRC2_OPERAND, inst->src[2]);
            set(E::THREE_SRC2_HSTRIDE, encode_hstride(inst->src[2].region.hstride));
         }

         if (inst->opcode != GEN_OP_BFN) {
            set(E::SRC0_NEGATE,       inst->src[0].negate);
            set(E::THREE_SRC1_NEGATE, inst->src[1].negate);
            set(E::THREE_SRC2_NEGATE, inst->src[2].negate);
            set(E::SRC0_ABS,          inst->src[0].abs);
            set(E::THREE_SRC1_ABS,    inst->src[1].abs);
            set(E::THREE_SRC2_ABS,    inst->src[2].abs);
         } else {
            set(E::BFN_FUNC_CONTROL, inst->boolean_func_ctrl);
         }

         break;
      }

      case GEN_FORMAT_DPAS_THREE_SRC: {
         assert(devinfo->verx10 >= 125);

         if constexpr (E::TYPE == GEN_ENCODING_XE)
            set(E::ACC_WR_CONTROL, inst->acc_wr_control);

         set(E::DPAS_RCOUNT,    inst->dpas.rcount - 1);
         set(E::DPAS_SDEPTH,    encode_sdepth(inst->dpas.sdepth));

         set(E::THREE_EXEC_DATA_TYPE, gen_type_is_float_or_bfloat(inst->dst.type));
         set(E::THREE_DST_TYPE,       encode_type_3src(inst->dst.type));
         set(E::THREE_SRC0_TYPE,      encode_type_3src(inst->src[0].type));
         set(E::THREE_SRC1_TYPE,      encode_type_3src(inst->src[1].type));
         set(E::THREE_SRC2_TYPE,      encode_type_3src(inst->src[2].type));

         set(E::DPAS_SRC1_SUBBYTE, inst->dpas.src1_subbyte);
         set(E::DPAS_SRC2_SUBBYTE, inst->dpas.src2_subbyte);

         /* TODO: Consider enabling the IsImm fields. */

         encode_direct_operand(E::THREE_DST_OPERAND,  inst->dst);
         encode_direct_operand(E::THREE_SRC0_OPERAND, inst->src[0]);
         encode_direct_operand(E::THREE_SRC1_OPERAND, inst->src[1]);
         encode_direct_operand(E::THREE_SRC2_OPERAND, inst->src[2]);

         break;
      }

      case GEN_FORMAT_SEND: {
         set(E::SEND_EOT,  inst->send.eot);
         set(E::SEND_SFID, inst->send.sfid);

         if constexpr (E::TYPE == GEN_ENCODING_XE)
            set(E::SEND_FUSION_CONTROL, inst->fusion_control);

         const bool skip_subnr = true;
         encode_direct_operand(E::DST_OPERAND,  inst->dst,    skip_subnr);
         encode_direct_operand(E::SRC0_OPERAND, inst->src[0], skip_subnr);
         encode_direct_operand(E::SRC1_OPERAND, inst->src[1], skip_subnr);

         set(E::SEND_DESC_IS_REG,    inst->send.desc_is_reg);
         set(E::SEND_EX_DESC_IS_REG, inst->send.ex_desc_is_reg);

         bool gather = false;
         if constexpr (E::TYPE >= GEN_ENCODING_XE2) {
            gather = devinfo->ver >= 30 &&
                     inst->src[0].file == GEN_ARF &&
                     inst->src[0].nr == GEN_ARF_SCALAR;

            if (gather) {
               /* TODO: Move check to validation. */
               assert((inst->src[0].subnr & 1) == 0);
               set(E::SEND_SRC0_SUB_NR, inst->src[0].subnr >> 1);
            }
         }

         bool xe2_ugm = false;
         if constexpr (E::TYPE >= GEN_ENCODING_XE2)
            xe2_ugm = inst->send.sfid == GEN_SFID_UGM;

         /* The SEND instruction ExBSO field does not exist with UGM on Gfx20+,
          * it is assumed.  See Bspec 56890 (r70933).
          */
         if (inst->send.ex_bso && !xe2_ugm)
            set(E::SEND_EX_BSO, 1);

         if (!gather && ((inst->send.ex_desc_is_reg && inst->send.ex_bso) ||
                          xe2_ugm))
            set(E::SEND_SRC1_LEN, inst->send.src1_len);

         if (!inst->send.desc_is_reg) {
            set(bits(123, 122), GET_BITS(inst->send.desc_imm, 31, 30));
            set(bits( 71,  67), GET_BITS(inst->send.desc_imm, 29, 25));
            set(bits( 55,  51), GET_BITS(inst->send.desc_imm, 24, 20));
            set(bits(121, 113), GET_BITS(inst->send.desc_imm, 19, 11));
            set(bits( 91,  81), GET_BITS(inst->send.desc_imm, 10, 0));
         }

         if (!inst->send.ex_desc_is_reg) {
            set(bits(127, 124), GET_BITS(inst->send.ex_desc_imm, 31, 28));
            set(bits( 97,  96), GET_BITS(inst->send.ex_desc_imm, 27, 26));
            set(bits( 65,  64), GET_BITS(inst->send.ex_desc_imm, 25, 24));
            set(bits( 47,  35), GET_BITS(inst->send.ex_desc_imm, 23, 11));

            assert(GET_BITS(inst->send.ex_desc_imm, 5, 0) == 0);

            if (!gather)
               set(bits(103,  99), GET_BITS(inst->send.ex_desc_imm, 10, 6));
            else
               assert(GET_BITS(inst->send.ex_desc_imm, 10, 6) == 0);

         } else {
            set(bits(42, 40), inst->send.ex_desc_subnr >> 2);

            if constexpr (E::TYPE >= GEN_ENCODING_XE2) {
               if (inst->send.ex_desc_imm_extra) {
                  set(bits(127, 124), (inst->send.ex_desc_imm_extra >> 28) & 0xF);
                  set(bits( 97,  96), (inst->send.ex_desc_imm_extra >> 26) & 0x3);
                  set(bits( 65,  64), (inst->send.ex_desc_imm_extra >> 24) & 0x3);
                  set(bits( 47,  43), (inst->send.ex_desc_imm_extra >> 19) & 0x1F);
                  set(bits( 39,  36), (inst->send.ex_desc_imm_extra >> 12) & 0xF);
               }
            }
         }

         break;
      }

      case GEN_FORMAT_BRANCH_ONE_SRC:
      case GEN_FORMAT_BRANCH_TWO_SRC: {
         set(E::BRANCH_CONTROL, inst->branch_control);

         if (inst->src[0].file != GEN_IMM) {
            set(E::SRC0_TYPE, encode_type(inst->src[0].file, inst->src[0].type));
            encode_direct_operand(E::SRC0_OPERAND, inst->src[0]);
         } else {
            set(E::SRC0_IS_IMM, 1);
            set(E::IMM_LO_32, inst->src[0].imm & 0xFFFFFFFF);
            if (gen_type_size_bytes(inst->src[0].type) > 4)
               set(E::IMM_HI_32, inst->src[0].imm >> 32);

            if (desc->format == GEN_FORMAT_BRANCH_TWO_SRC) {
               assert(gen_type_size_bytes(inst->src[0].type) <= 4);
               set(E::SRC1_IS_IMM, 1);
               set(E::IMM_HI_32, inst->src[1].imm & 0xFFFFFFFF);
            }
         }
         break;
      }

      case GEN_FORMAT_ILLEGAL:
      case GEN_FORMAT_NOP:
         break;
      }
   }

private:
   inline void
   set(const gen_range &bits, uint64_t value)
   {
      unsigned high = bits.hi;
      unsigned low = bits.lo;

      assume(high < 128);
      assume(high >= low);
      const unsigned word = high / 64;
      assert(word == low / 64);

      high %= 64;
      low %= 64;

      const uint64_t mask = (~0ull >> (64 - (high - low + 1))) << low;

      /* Make sure the supplied value actually fits in the given bitfield. */
      assert((value & (mask >> low)) == value);

      raw->data[word] = (raw->data[word] & ~mask) | (value << low);
   }

   inline void
   set(const gen_ranges<1> &ranges, uint64_t value)
   {
      set(ranges[0], value);
   }

   inline void
   set(const gen_ranges<2> &ranges, uint64_t value)
   {
      unsigned num_bits = (ranges[1].hi - ranges[1].lo + 1);
      assume(num_bits <= 64);
      uint64_t mask = ~0ull >> (64 - num_bits);
      set(ranges[1], value & mask);
      value >>= num_bits;
      set(ranges[0], value);
   }

   inline void
   encode_direct_operand(const gen_range &bits, const gen_operand &o, bool skip_subnr = false)
   {
      unsigned subnr = o.subnr;
      if constexpr (E::TYPE >= GEN_ENCODING_XE2)
         subnr >>= 1;

      set(bits( 0), o.file == GEN_GRF ? 1 : 0);
      if (!skip_subnr)
         set(bits( 5, 1), subnr);
      set(bits(13, 6), o.nr);
   }

   inline void
   encode_indirect_operand(const gen_range &bits, const gen_operand &o)
   {
      unsigned raw_addr_imm;
      if constexpr (E::TYPE >= GEN_ENCODING_XE2)
         raw_addr_imm = ((unsigned)o.addr_imm & ((1 << 11) - 1)) >> 1;
      else
         raw_addr_imm = (unsigned)o.addr_imm & ((1 << 10) - 1);

      set(bits(13, 10), o.subnr);
      set(bits( 9,  0), raw_addr_imm);
   }

   static inline unsigned
   encode_vstride(unsigned value)
   {
      unsigned vstride =
         value == GEN_VSTRIDE_ONE_DIMENSIONAL ? 0xF : cvt(value);

      if constexpr (E::TYPE >= GEN_ENCODING_XE2)
         vstride &= 0x7;

      return vstride;
   }

   static inline unsigned
   encode_hstride(unsigned value)
   {
      return cvt(value);
   }

   static inline unsigned
   encode_width(unsigned value)
   {
      return cvt(value) - 1;
   }

   inline unsigned
   encode_type(gen_file file, gen_reg_type type)
   {
      assert(file != GEN_IMM ||
             gen_type_is_vector_imm(type) ||
             gen_type_size_bits(type) >= 16);

      if (type == GEN_TYPE_INVALID)
         return GEN_INVALID_HW_REG_TYPE;

      if (gen_type_size_bits(type) == 64 &&
          !(gen_type_is_int(type) ? devinfo->has_64bit_int
                                  : devinfo->has_64bit_float))
         return GEN_INVALID_HW_REG_TYPE;

      if (gen_type_is_float_or_bfloat(type) &&
          gen_type_size_bits(type) == 8 &&
          !devinfo->has_fp8)
         return GEN_INVALID_HW_REG_TYPE;

      if (gen_type_is_bfloat(type) && !devinfo->has_bfloat16)
         return GEN_INVALID_HW_REG_TYPE;

      if (gen_type_is_vector_imm(type))
         return type & ~(GEN_TYPE_VECTOR | GEN_TYPE_SIZE_MASK);

      if (type == GEN_TYPE_BF8)
         return 0b1000;
      if (type == GEN_TYPE_HF8)
         return 0b1100;

      return type & (GEN_TYPE_BASE_MASK | GEN_TYPE_SIZE_MASK);
   }

   inline unsigned
   encode_type_3src(gen_reg_type type)
   {
      if (gen_type_is_bfloat(type) && !devinfo->has_bfloat16)
         return GEN_INVALID_HW_REG_TYPE;

      /* size mask and SINT type bit match exactly */
      return type & 0b111;
   }

   static inline unsigned
   encode_file(gen_file file)
   {
      switch (file) {
      case GEN_BAD_FILE: UNREACHABLE("invalid reg file");
      case GEN_ARF:      return 0x0;
      case GEN_GRF:      return 0x1;
      case GEN_IMM:      return 0x3;
      }
   }
};

gen_reg_type
xe_decode_type(const intel_device_info *devinfo, gen_file file,
               unsigned hw_type)
{
   if (hw_type >= (1 << 4))
      return GEN_TYPE_INVALID;

   static const gen_reg_type tbl[16] = {
      [0b0000] = GEN_TYPE_UB, /* or UV */
      [0b0001] = GEN_TYPE_UW,
      [0b0010] = GEN_TYPE_UD,
      [0b0011] = GEN_TYPE_UQ,
      [0b0100] = GEN_TYPE_B, /* or V */
      [0b0101] = GEN_TYPE_W,
      [0b0110] = GEN_TYPE_D,
      [0b0111] = GEN_TYPE_Q,
      [0b1000] = GEN_TYPE_BF8, /* or VF */
      [0b1001] = GEN_TYPE_HF,
      [0b1010] = GEN_TYPE_F,
      [0b1011] = GEN_TYPE_DF,
      [0b1100] = GEN_TYPE_HF8,
      [0b1101] = GEN_TYPE_BF,
      [0b1110] = GEN_TYPE_INVALID,
      [0b1111] = GEN_TYPE_INVALID,
   };

   enum gen_reg_type t = tbl[hw_type];

   if (file == GEN_IMM) {
      switch (t) {
      case GEN_TYPE_UB:  return GEN_TYPE_UV;
      case GEN_TYPE_B:   return GEN_TYPE_V;
      case GEN_TYPE_BF8: return GEN_TYPE_VF;
      case GEN_TYPE_HF8: return GEN_TYPE_VF;
      default:           break;
      }
   }

   if ((t == GEN_TYPE_HF8 || t == GEN_TYPE_BF8) &&
       !devinfo->has_fp8)
      return GEN_TYPE_INVALID;
   if (t == GEN_TYPE_BF && !devinfo->has_bfloat16)
      return GEN_TYPE_INVALID;

   return t;
}

/* E is the struct with the Encoding fields and type. */
template <typename E>
struct gen_decoder {
   const intel_device_info *devinfo;

   gen_inst *inst;
   const gen_raw_inst *raw;
   const gen_inst_description *desc;
   void *mem_ctx;
   gen_error *errors;
   int num_errors;
   int error_index;

   gen_decoder(const intel_device_info *devinfo, void *mem_ctx)
      : devinfo(devinfo),
        mem_ctx(mem_ctx),
        errors(nullptr),
        num_errors(0),
        error_index(0)
        {}

   void PRINTFLIKE(2, 3)
   report_errorf(const char *fmt, ...)
   {
      errors = reralloc(mem_ctx, errors, gen_error, num_errors + 1);
      errors[num_errors].index = error_index;

      va_list args;
      va_start(args, fmt);
      errors[num_errors].msg = ralloc_vasprintf(mem_ctx, fmt, args);
      va_end(args);

      num_errors++;
   }

   bool
   decode_many(gen_decode_params *params)
   {
      int decoded = 0;

      void *uncompacted;
      gen_uncompact(params, uncompacted);
      const size_t uncompact_size = sizeof(gen_raw_inst) * params->num_insts;

      const uint64_t *raw = (uint64_t *)uncompacted;
      const uint64_t *raw_end = raw + (uncompact_size / 8);

      decoded = 0;
      while (raw < raw_end) {
         if (gen_raw_is_compact((void *)raw)) {
            UNREACHABLE("Compact instructions can't be decoded!");
            return false;
         } else {
            decode(&params->insts[decoded], (gen_raw_inst *)raw);
         }

         raw += 2;
         decoded++;
         error_index++;
      }

      params->num_insts = decoded;
      params->errors = errors;
      params->num_errors = num_errors;
      return num_errors == 0;
   }

   void
   decode(gen_inst *inst, const gen_raw_inst *raw)
   {
      this->inst = inst;
      this->raw = raw;
      this->desc = &E::hw_to_description[get(E::HW_OPCODE)];

      memset(inst, 0, sizeof(*inst));

      inst->opcode = desc->gen_op;

      inst->swsb = gen_swsb_decode(devinfo, gen_inst_is_unordered(devinfo, inst),
                                   get(E::SWSB), inst->opcode);

      const unsigned encoded_exec_size = get(E::EXEC_SIZE);
      if (encoded_exec_size > 5)
         RETURN_ERROR("invalid execution size");
      inst->exec_size = 1 << encoded_exec_size;

      if constexpr (E::TYPE == GEN_ENCODING_XE)
         inst->chan_offset = get(E::CHAN_OFFSET) * 4;
      else
         inst->chan_offset = get(E::CHAN_OFFSET) * 8;

      inst->flag_subnr     = get(E::FLAG_SUBNR);
      inst->flag_nr        = get(E::FLAG_NR);
      inst->pred_control   = (gen_predicate) get(E::PRED_CONTROL);
      inst->pred_inv       = get(E::PRED_INV);
      inst->debug_control  = get(E::DEBUG_CONTROL);
      inst->no_mask        = get(E::NO_MASK);
      inst->atomic_control = get(E::ATOMIC_CONTROL);

      if (gen_inst_has_saturate(desc->format, inst))
         inst->saturate = get(E::SATURATE);

      switch (desc->format) {
      case GEN_FORMAT_BASIC_ONE_SRC:
      case GEN_FORMAT_BASIC_TWO_SRC: {
         if (desc->has_dst) {
            inst->dst.indirect       = get(E::DST_ADDRESS_MODE);
            inst->dst.region.hstride = decode_hstride(get(E::DST_HSTRIDE));

            if (inst->dst.indirect) {
               inst->dst.file = GEN_GRF;
               decode_indirect_operand(E::DST_OPERAND, inst->dst);
               if constexpr (E::TYPE >= GEN_ENCODING_XE2)
                  inst->dst.addr_imm |= get(E::DST_OPERAND_EXTRA);

            } else {
               decode_direct_operand(E::DST_OPERAND, inst->dst);
               if constexpr (E::TYPE >= GEN_ENCODING_XE2)
                  inst->dst.subnr |= get(E::DST_OPERAND_EXTRA);
            }
         }

         if constexpr (E::TYPE == GEN_ENCODING_XE)
            inst->acc_wr_control = get(E::ACC_WR_CONTROL);

         inst->src[0].indirect     = get(E::SRC0_ADDRESS_MODE);
         inst->src[0].negate       = get(E::SRC0_NEGATE);
         inst->src[0].abs          = get(E::SRC0_ABS);

         int imm_src = -1;
         if (get(E::SRC0_IS_IMM)) {
            imm_src = 0;
            inst->src[0].file = GEN_IMM;

         } else {
            if (inst->src[0].indirect) {
               decode_indirect_operand(E::SRC0_OPERAND, inst->src[0]);
               inst->src[0].file = GEN_GRF;
               if constexpr (E::TYPE >= GEN_ENCODING_XE2)
                  inst->src[0].addr_imm |= get(E::SRC0_OPERAND_EXTRA);

            } else {
               decode_direct_operand(E::SRC0_OPERAND, inst->src[0]);
               if constexpr (E::TYPE >= GEN_ENCODING_XE2)
                  inst->src[0].subnr |= get(E::SRC0_OPERAND_EXTRA);
            }

            inst->src[0].region.vstride = decode_vstride(get(E::SRC0_VSTRIDE));
            inst->src[0].region.width   = decode_width(get(E::SRC0_WIDTH));
            inst->src[0].region.hstride = decode_hstride(get(E::SRC0_HSTRIDE));
         }

         if (desc->format == GEN_FORMAT_BASIC_TWO_SRC) {
            inst->src[1].indirect = get(E::SRC1_ADDRESS_MODE);

            if (get(E::SRC1_IS_IMM)) {
               assert(imm_src == - 1);
               imm_src = 1;
               inst->src[1].file = GEN_IMM;

            } else {
               inst->src[1].negate   = get(E::SRC1_NEGATE);
               inst->src[1].abs      = get(E::SRC1_ABS);

               if (inst->src[1].indirect)
                  decode_indirect_operand(E::SRC1_OPERAND, inst->src[1]);
               else
                  decode_direct_operand(E::SRC1_OPERAND, inst->src[1]);

               inst->src[1].region.vstride = decode_vstride(get(E::SRC1_VSTRIDE));
               inst->src[1].region.width   = decode_width(get(E::SRC1_WIDTH));
               inst->src[1].region.hstride = decode_hstride(get(E::SRC1_HSTRIDE));
            }
         }

         inst->dst.type    = decode_type(inst->dst.file,    get(E::DST_TYPE));
         inst->src[0].type = decode_type(inst->src[0].file, get(E::SRC0_TYPE));
         if (desc->format == GEN_FORMAT_BASIC_TWO_SRC)
            inst->src[1].type = decode_type(inst->src[1].file, get(E::SRC1_TYPE));
         else
            inst->src[1].file = GEN_BAD_FILE;

         if (imm_src != -1) {
            uint64_t value = get(E::IMM_LO_32);
            if (gen_type_size_bytes(inst->src[imm_src].type) > 4)
               value |= get(E::IMM_HI_32) << 32;
            inst->src[imm_src].imm = value;
         }

         break;
      }

      case GEN_FORMAT_BASIC_THREE_SRC: {
         if constexpr (E::TYPE == GEN_ENCODING_XE)
            inst->acc_wr_control = get(E::ACC_WR_CONTROL);

         const bool is_float = get(E::THREE_EXEC_DATA_TYPE);
         inst->dst.type    = decode_type_3src(get(E::THREE_DST_TYPE), is_float);
         inst->src[0].type = decode_type_3src(get(E::THREE_SRC0_TYPE), is_float);
         inst->src[1].type = decode_type_3src(get(E::THREE_SRC1_TYPE), is_float);
         inst->src[2].type = decode_type_3src(get(E::THREE_SRC2_TYPE), is_float);

         decode_direct_operand(E::THREE_DST_OPERAND, inst->dst);
         inst->dst.region.hstride = get(E::THREE_DST_HSTRIDE) + 1;

         const bool src0_is_imm = get(E::SRC0_IS_IMM);
         const bool src2_is_imm = get(E::THREE_SRC2_IS_IMM);

         if (src0_is_imm) {
            inst->src[0].file = GEN_IMM;
            inst->src[0].imm = get(E::THREE_SRC0_IMM);
         } else {
            decode_direct_operand(E::THREE_SRC0_OPERAND, inst->src[0]);

            const unsigned encoded_src0_vstride = get(E::THREE_SRC0_VSTRIDE);
            inst->src[0].region.vstride = DECODE_VSTRIDE_3SRC(encoded_src0_vstride);

            inst->src[0].region.hstride = decode_hstride(get(E::THREE_SRC0_HSTRIDE));
            inst->src[0].region.width = gen_implied_width_for_3src_a1(inst->src[0].region.vstride, inst->src[0].region.hstride);
         }

         decode_direct_operand(E::THREE_SRC1_OPERAND, inst->src[1]);

         const unsigned encoded_src1_vstride = get(E::THREE_SRC1_VSTRIDE);
         inst->src[1].region.vstride = DECODE_VSTRIDE_3SRC(encoded_src1_vstride);

         inst->src[1].region.hstride = decode_hstride(get(E::THREE_SRC1_HSTRIDE));
         inst->src[1].region.width = gen_implied_width_for_3src_a1(inst->src[1].region.vstride, inst->src[1].region.hstride);

         if (src2_is_imm) {
            inst->src[2].file = GEN_IMM;
            inst->src[2].imm = get(E::THREE_SRC2_IMM);
         } else {
            decode_direct_operand(E::THREE_SRC2_OPERAND, inst->src[2]);

            inst->src[2].region.hstride = decode_hstride(get(E::THREE_SRC2_HSTRIDE));
            inst->src[2].region.width = gen_implied_width_for_3src_a1(inst->src[2].region.vstride, inst->src[2].region.hstride);
         }

         if (inst->opcode != GEN_OP_BFN) {
            inst->src[0].negate = get(E::SRC0_NEGATE);
            inst->src[1].negate = get(E::THREE_SRC1_NEGATE);
            inst->src[2].negate = get(E::THREE_SRC2_NEGATE);
            inst->src[0].abs    = get(E::SRC0_ABS);
            inst->src[1].abs    = get(E::THREE_SRC1_ABS);
            inst->src[2].abs    = get(E::THREE_SRC2_ABS);
         } else {
            inst->boolean_func_ctrl = get(E::BFN_FUNC_CONTROL);
         }

         break;
      }

      case GEN_FORMAT_DPAS_THREE_SRC: {
         assert(devinfo->verx10 >= 125);

         if constexpr (E::TYPE == GEN_ENCODING_XE)
            inst->acc_wr_control = get(E::ACC_WR_CONTROL);

         inst->dpas.rcount    = get(E::DPAS_RCOUNT) + 1;
         inst->dpas.sdepth    = decode_sdepth(get(E::DPAS_SDEPTH));

         const bool is_float = get(E::THREE_EXEC_DATA_TYPE);
         inst->dst.type    = decode_type_3src(get(E::THREE_DST_TYPE), is_float);
         inst->src[0].type = decode_type_3src(get(E::THREE_SRC0_TYPE), is_float);
         inst->src[1].type = decode_type_3src(get(E::THREE_SRC1_TYPE), is_float);
         inst->src[2].type = decode_type_3src(get(E::THREE_SRC2_TYPE), is_float);

         inst->dpas.src1_subbyte = get(E::DPAS_SRC1_SUBBYTE);
         inst->dpas.src2_subbyte = get(E::DPAS_SRC2_SUBBYTE);

         /* TODO: Consider enabling the IsImm fields. */

         decode_direct_operand(E::THREE_DST_OPERAND,  inst->dst);
         decode_direct_operand(E::THREE_SRC0_OPERAND, inst->src[0]);
         decode_direct_operand(E::THREE_SRC1_OPERAND, inst->src[1]);
         decode_direct_operand(E::THREE_SRC2_OPERAND, inst->src[2]);

         break;
      }

      case GEN_FORMAT_SEND: {
         inst->send.eot  = get(E::SEND_EOT);
         inst->send.sfid = (gen_sfid)get(E::SEND_SFID);

         if constexpr (E::TYPE == GEN_ENCODING_XE)
            inst->fusion_control = get(E::SEND_FUSION_CONTROL);

         const bool skip_subnr = true;
         decode_direct_operand(E::DST_OPERAND,  inst->dst,    skip_subnr);
         decode_direct_operand(E::SRC0_OPERAND, inst->src[0], skip_subnr);
         decode_direct_operand(E::SRC1_OPERAND, inst->src[1], skip_subnr);

         inst->send.desc_is_reg    = get(E::SEND_DESC_IS_REG);
         inst->send.ex_desc_is_reg = get(E::SEND_EX_DESC_IS_REG);

         inst->dst.type    = GEN_TYPE_D;
         inst->src[0].type = GEN_TYPE_D;
         inst->src[1].type = GEN_TYPE_D;

         gen_range bits = { 127, 0 };

         if (!inst->send.desc_is_reg) {
            inst->send.desc_imm = get(bits(123, 122)) << 30 |
                                  get(bits( 71,  67)) << 25 |
                                  get(bits( 55,  51)) << 20 |
                                  get(bits(121, 113)) << 11 |
                                  get(bits( 91,  81));
         }

         bool gather = false;
         if constexpr (E::TYPE >= GEN_ENCODING_XE2) {
            gather = devinfo->ver >= 30 &&
                     inst->src[0].file == GEN_ARF &&
                     inst->src[0].nr == GEN_ARF_SCALAR;

            if (gather)
               inst->src[0].subnr = get(E::SEND_SRC0_SUB_NR) << 1;
         }

         if (devinfo->verx10 >= 125) {
            /* The send instruction ExBSO field does not exist with UGM on Gfx20+,
             * it is assumed.  See Bspec 56890 (r70933).
             */
            bool xe2_ugm = false;
            if constexpr (E::TYPE >= GEN_ENCODING_XE2)
               xe2_ugm = inst->send.sfid == GEN_SFID_UGM;

            inst->send.ex_bso = xe2_ugm || get(E::SEND_EX_BSO);

            if (!gather && ((inst->send.ex_desc_is_reg && inst->send.ex_bso) ||
                            xe2_ugm))
               inst->send.src1_len = get(E::SEND_SRC1_LEN);
         }

         if (!inst->send.ex_desc_is_reg) {
            inst->send.ex_desc_imm = get(bits(127, 124)) << 28 |
                                     get(bits( 97,  96)) << 26 |
                                     get(bits( 65,  64)) << 24 |
                                     get(bits( 47,  35)) << 11;

            if (!gather)
               inst->send.ex_desc_imm |= get(bits(103, 99)) << 6;

          } else {
            inst->send.ex_desc_subnr = get(bits(42, 40)) << 2;

            if constexpr (E::TYPE >= GEN_ENCODING_XE2) {
               inst->send.ex_desc_imm_extra =
                  get(bits(127, 124)) << 28 |
                  get(bits( 97,  96)) << 26 |
                  get(bits( 65,  64)) << 24 |
                  get(bits( 47,  43)) << 19 |
                  get(bits( 39,  36)) << 12;
            }
         }

         break;
      }

      case GEN_FORMAT_BRANCH_ONE_SRC:
      case GEN_FORMAT_BRANCH_TWO_SRC: {
         inst->branch_control = get(E::BRANCH_CONTROL);

         if (!get(E::SRC0_IS_IMM)) {
            inst->src[0].type = decode_type(GEN_GRF, get(E::SRC0_TYPE));
            decode_direct_operand(E::SRC0_OPERAND, inst->src[0]);
         } else {
            inst->src[0].file = GEN_IMM;
            inst->src[0].type = decode_type(inst->src[0].file, get(E::SRC0_TYPE));

            uint64_t value = get(E::IMM_LO_32);
            if (gen_type_size_bytes(inst->src[0].type) > 4)
               value |= get(E::IMM_HI_32) << 32;
            inst->src[0].imm = value;

            if (desc->format == GEN_FORMAT_BRANCH_TWO_SRC) {
               inst->src[1].file = GEN_IMM;
               inst->src[1].type = inst->src[0].type;
               inst->src[1].imm  = get(E::IMM_HI_32);
            }
         }
         break;
      }

      case GEN_FORMAT_ILLEGAL:
      case GEN_FORMAT_NOP:
         break;
      }

      /* Decode cmod after src0 was decoded so we can detect the
       * imm64 form, which overlaps the cmod field.
       */
      if (inst->opcode == GEN_OP_MATH) {
         inst->math.func = (gen_math)get(E::MATH_FC);
      } else if (inst->opcode == GEN_OP_SYNC) {
         inst->sync.func = (gen_sync_func)get(E::SYNC_CTRL);
      } else if (inst->opcode == GEN_OP_BFN) {
         const unsigned encoded_bfn_cmod = get(E::BFN_COND_MODIFIER);
         inst->cmod = encoded_bfn_cmod == 1 ? GEN_CONDITION_ZE :
                      encoded_bfn_cmod == 2 ? GEN_CONDITION_GT :
                      encoded_bfn_cmod == 3 ? GEN_CONDITION_LT :
                                              GEN_CONDITION_NONE;
      } else if (gen_inst_has_cond_modifier(devinfo, desc->format, inst)) {
         inst->cmod = (gen_condition)get(E::COND_MODIFIER);
      }

      if (desc->has_dst) {
         ERROR_IF(inst->dst.type == GEN_TYPE_INVALID,
               "Invalid destination register type encoding.");
      }

      const unsigned num_sources = gen_inst_num_sources(devinfo, inst);
      for (unsigned i = 0; i < num_sources; i++) {
         ERROR_IF(inst->src[i].type == GEN_TYPE_INVALID,
               "Invalid source register type encoding.");
      }
   }


private:
   inline uint64_t
   get(const gen_range &bits) const
   {
      unsigned high = bits.hi;
      unsigned low = bits.lo;

      assume(high < 128);
      assume(high >= low);
      /* We assume the field doesn't cross 64-bit boundaries. */
      const unsigned word = high / 64;
      assert(word == low / 64);

      high %= 64;
      low %= 64;

      const uint64_t mask = (~0ull >> (64 - (high - low + 1)));

      return (raw->data[word] >> low) & mask;
   }

   inline uint64_t
   get(const gen_ranges<1> &ranges) const
   {
      return get(ranges[0]);
   }

   inline uint64_t
   get(const gen_ranges<2> &ranges) const
   {
      return
         (get(ranges[0]) << (ranges[1].hi - ranges[1].lo + 1)) |
         get(ranges[1]) ;
   }

   inline void
   decode_direct_operand(const gen_range &bits, gen_operand &o, bool skip_subnr = false)
   {
      o.file  = get(bits( 0)) ? GEN_GRF : GEN_ARF;
      o.nr    = get(bits(13, 6));

      if (!skip_subnr) {
         o.subnr = get(bits( 5, 1));
         if constexpr (E::TYPE >= GEN_ENCODING_XE2)
            o.subnr <<= 1;
      }
   }

   inline void
   decode_indirect_operand(const gen_range &bits, gen_operand &o)
   {
      unsigned shift = 22;
      uint64_t addr_imm_bits = get(bits(9, 0));

      if constexpr (E::TYPE >= GEN_ENCODING_XE2) {
         shift--;
         addr_imm_bits = addr_imm_bits << 1;
      }

      o.addr_imm = static_cast<int32_t>(addr_imm_bits << shift) >> shift;
      o.subnr    = get(bits(13, 10));

   }

   static inline unsigned
   decode_vstride(unsigned v)
   {
      if constexpr (E::TYPE >= GEN_ENCODING_XE2) {
         if (v == 0x7)
            return GEN_VSTRIDE_ONE_DIMENSIONAL;
      } else {
         if (v == 0xF)
            return GEN_VSTRIDE_ONE_DIMENSIONAL;
      }
      return v ? 1 << (v - 1) : 0;
   }

   static inline unsigned
   decode_hstride(unsigned v)
   {
      return v ? 1 << (v - 1) : 0;
   }

   static inline unsigned
   decode_width(unsigned v)
   {
      return 1 << v;
   }

   static inline gen_file
   decode_file(unsigned hw_file)
   {
      switch (hw_file) {
      case 0:  return GEN_ARF;
      case 1:  return GEN_GRF;
      default: return GEN_IMM;
      }
   }

   inline gen_reg_type
   decode_type(gen_file file, unsigned hw_type)
   {
      return xe_decode_type(devinfo, file, hw_type);
   }

   inline gen_reg_type
   decode_type_3src(unsigned hw_type, bool is_float)
   {
      unsigned size_field = hw_type & GEN_TYPE_SIZE_MASK;
      unsigned base_field = hw_type & GEN_TYPE_BASE_MASK;
      if (is_float) {
         base_field |= GEN_TYPE_BASE_FLOAT;
         if (base_field == GEN_TYPE_BASE_BFLOAT && !devinfo->has_bfloat16)
            return GEN_TYPE_INVALID;
      }
      return (gen_reg_type) (base_field | size_field);
   }
};

template <typename E>
static int
gen_find_shader_size_xe(const uint64_t *raw,
                        const uint64_t *raw_start,
                        const uint64_t *raw_end)
{
   /* TODO: Stops at the first SEND-with-EOT, so shaders with multiple EOT
    * paths may be truncated.
    */
   while (raw_end == NULL || raw < raw_end) {
      const bool compact = gen_raw_is_compact(raw);
      const uint32_t hw_opcode = gen_raw_get_opcode(raw);
      const unsigned inst_words = compact ? 1 : 2;

      if (raw_end != NULL && raw + inst_words > raw_end)
         break;

      raw += inst_words;

      if (hw_opcode == E::gen_to_description[GEN_OP_ILLEGAL].hw_opcode)
         break;

      if (!compact &&
          (hw_opcode == E::gen_to_description[GEN_OP_SEND].hw_opcode ||
           hw_opcode == E::gen_to_description[GEN_OP_SENDC].hw_opcode) &&
          (raw - inst_words)[0] & (UINT64_C(1) << ((gen_range)E::SEND_EOT).lo))
         break;
   }

   return (raw - raw_start) * sizeof(*raw);
}

int
gen_find_shader_size(const struct intel_device_info *devinfo,
                     const void *assembly,
                     int start,
                     int end_bound)
{
   assert(devinfo);
   assert(assembly);
   assert(start >= 0);
   assert(end_bound == 0 || end_bound >= start);

   const uint64_t *raw = (const uint64_t *)((const uint8_t *)assembly + start);
   const uint64_t *raw_start = raw;
   const uint64_t *raw_end = end_bound == 0 ? NULL :
      (const uint64_t *)((const uint8_t *)assembly + end_bound);

   if (devinfo->ver < 12)
      return gen_find_shader_size_pre_xe(devinfo, raw, raw_start, raw_end);
   else if (devinfo->ver < 20)
      return gen_find_shader_size_xe<gen_encoding_xe>(raw, raw_start, raw_end);
   else
      return gen_find_shader_size_xe<gen_encoding_xe2>(raw, raw_start, raw_end);
}

bool
gen_encode(gen_encode_params *params)
{
   assert(params->devinfo);
   assert(params->mem_ctx);
   assert(params->insts);
   assert(params->raw_bytes);
   assert(params->errors == NULL);

   if (!params->raw_bytes)
      return false;

   /* TODO: Implement params->compact_all.  For now it is being ignored. */

   const intel_device_info *devinfo = params->devinfo;

   if (params->num_insts == 0) {
      params->raw_bytes_size = 0;
      return true;
   }

   if (!params->skip_validation) {
      gen_validate_params val_params = {
         .devinfo   = devinfo,
         .insts     = params->insts,
         .num_insts = params->num_insts,
         .mem_ctx   = params->mem_ctx,
      };

      /* Early return if is not valid. */
      if (!gen_validate(&val_params)) {
         params->errors     = val_params.errors;
         params->num_errors = val_params.num_errors;
         return false;
      }
   }

   const int required_size = params->num_insts * sizeof(gen_raw_inst);

   if (params->raw_bytes_size < required_size)
      return false;

   if (devinfo->ver < 12) {
      return gen_encode_pre_xe(params);
   } else if (devinfo->ver < 20) {
      auto e = gen_encoder<gen_encoding_xe>(devinfo);
      return e.encode_many(params);
   } else {
      auto e = gen_encoder<gen_encoding_xe2>(devinfo);
      return e.encode_many(params);
   }
}

bool
gen_scan_raw_layout(gen_scan_raw_layout_params *params)
{
   assert(params);
   assert(params->raw_bytes || params->raw_bytes_size == 0);
   assert(params->raw_bytes_size >= 0);
   assert(params->raw_bytes_size % 8 == 0);

   if (params->raw_bytes_size == 0) {
      params->num_insts = 0;
      return true;
   }

   const int max_insts = params->num_insts;

   const uint64_t *raw       = (const uint64_t *)params->raw_bytes;
   const uint64_t *raw_start = raw;
   const uint64_t *raw_end   = raw + (params->raw_bytes_size / 8);

   int count = 0;

   while (raw < raw_end) {
      /* Decide if we walk 64-bit or 128-bit based whether encoded
       * instruction is compacted.
       */
      const bool compact = gen_raw_is_compact(raw);
      const unsigned inst_words = compact ? 1 : 2;

      if (raw + inst_words > raw_end)
         break;
      if (count == max_insts) {
         /* When compacting we pad with a compact-nop to align program to an
          * uncompacted instruction boundary. We can ignore this
          * instruction.
          */
         if (compact && (raw + inst_words) == raw_end)
            break;
         return false;
      }

      if (params->layouts) {
         params->layouts[count].offset = (raw - raw_start) * sizeof(*raw);
         params->layouts[count].was_compacted = compact;
      }

      raw += inst_words;
      count++;
   }

   params->num_insts = count;
   params->end_offset = (raw - raw_start) * sizeof(*raw);
   return true;
}

static int
gen_count_instructions(const void *raw_bytes, int raw_bytes_size)
{
   gen_scan_raw_layout_params params = {
      .raw_bytes = raw_bytes,
      .raw_bytes_size = raw_bytes_size,
      .num_insts = raw_bytes_size / 8,
   };
   return gen_scan_raw_layout(&params) ? params.num_insts : 0;
}

bool
gen_decode(gen_decode_params *params)
{
   assert(params->devinfo);
   assert(params->mem_ctx);
   assert(params->raw_bytes);
   assert(params->insts == NULL);
   assert(params->errors == NULL);
   assert(params->raw_bytes_size % 8 == 0);

   const intel_device_info *devinfo = params->devinfo;

   if (params->raw_bytes_size == 0)
      return true;

   params->num_insts = gen_count_instructions(params->raw_bytes, params->raw_bytes_size);
   if (params->num_insts == 0)
      return true;

   params->insts = rzalloc_array(params->mem_ctx, gen_inst, params->num_insts);

   if (devinfo->ver < 12) {
      return gen_decode_pre_xe(params);
   } else if (devinfo->ver < 20) {
      auto d = gen_decoder<gen_encoding_xe>(devinfo, params->mem_ctx);
      return d.decode_many(params);
   } else {
      auto d = gen_decoder<gen_encoding_xe2>(devinfo, params->mem_ctx);
      return d.decode_many(params);
   }
}

void
gen_update_reloc_imm(const struct intel_device_info *devinfo,
                     void *inst, uint32_t value)
{
   assert(devinfo);
   assert(!gen_raw_is_compact(inst));

   static_assert(gen_encoding_xe ::gen_to_description[GEN_OP_MOV].hw_opcode ==
                 gen_encoding_xe2::gen_to_description[GEN_OP_MOV].hw_opcode);

   ASSERTED unsigned mov_opcode = devinfo->ver < 12
      ? gen_encoding_pre_xe::gen_to_description[GEN_OP_MOV].hw_opcode
      : gen_encoding_xe    ::gen_to_description[GEN_OP_MOV].hw_opcode;
   assert(gen_raw_get_opcode(inst) == mov_opcode);

   /* The position of the 32-bit immediate is the same across all
    * versions, so we can just rewrite the bits here.
    */
   uint32_t *inst_dwords = (uint32_t *)inst;
   inst_dwords[3] = value;
}
