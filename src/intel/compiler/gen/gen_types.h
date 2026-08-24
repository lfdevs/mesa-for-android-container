/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "gen_enums.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gen_region {
   uint8_t vstride;
   uint8_t width;
   uint8_t hstride;
} gen_region;

typedef struct gen_operand {
   gen_file file:2;
   gen_reg_type type:5;
   bool indirect:1;
   bool negate:1;
   bool abs:1;
   bool rep_ctrl:1;

   gen_region region;

   uint8_t swizzle;
   uint8_t writemask;

   /* In bytes. */
   uint8_t subnr;

   union {
      /* Physical register number. */
      unsigned nr;
      int addr_imm;
      uint64_t imm;
   };
} gen_operand;

typedef struct gen_swsb {
   unsigned regdist : 3;
   gen_pipe pipe : 3;
   unsigned sbid : 5;
   gen_sbid_mode mode : 3;
   unsigned pad : 2;
} PACKED gen_swsb;
static_assert(sizeof(gen_swsb) == 2, "packed");

typedef struct gen_message_desc {
   unsigned msg_length;
   unsigned response_length;
   bool header_present;
} gen_message_desc;

typedef struct gen_lsc_desc {
   enum lsc_opcode op;
   enum lsc_addr_surface_type addr_type;
   enum lsc_addr_size addr_size;

   /* Common for all non-fence ops. */
   enum lsc_data_size data_size;
   unsigned cache_ctrl;

   /* Non-CMask. */
   enum lsc_vect_size vect_size;
   bool transpose;

   /* Block2d only. */
   bool vnni;

   /* CMask only. */
   enum lsc_cmask cmask;

   /* Fence only. */
   struct {
      enum lsc_fence_scope scope;
      enum lsc_flush_type flush_type;
      bool route_to_lsc;
   } fence;
} gen_lsc_desc;

/* Classic pre-Xe2 URB descriptor. Xe2+ URB uses the LSC descriptor
 * encoding and is handled via gen_lsc_desc instead.
 *
 *   desc[3:0]   opcode
 *   desc[14:4]  global_offset (11 bits)
 *   desc[15]    swizzle: "masked" on simd8_read/simd8_write,
 *               "interleave" on atomics
 *   desc[17]    per_slot_offset_present
 */
typedef struct gen_urb_desc {
   enum gen_urb_opcode op;
   unsigned global_offset;
   bool swizzle;
   bool per_slot_offset;
} gen_urb_desc;

/* Sampler descriptor (pre-LSC; Xe2+ sampler is not LSC).
 *
 *   desc[7:0]    bti
 *   desc[11:8]   sampler_index
 *   desc[16:12]  msg_type[4:0]
 *   desc[18:17]  simd_mode[1:0]
 *   desc[29]     simd_mode[2]
 *   desc[30]     return_hp
 *   desc[31]     msg_type[5]   (Xe2+ only)
 */
typedef struct gen_sampler_desc {
   unsigned msg_type;      /* 5 bits pre-Xe2, 6 bits on Xe2+ */
   unsigned simd_mode;     /* 3 bits */
   unsigned bti;           /* 8 bits */
   unsigned sampler_index; /* 4 bits */
   bool return_hp;
} gen_sampler_desc;

/* HDC (classic DataPort) descriptor. Shared layout used by HDC0, HDC1,
 * and HDC_READ_ONLY SFIDs. The per-SFID dispatch happens on inst->send.sfid;
 * this struct just captures the common descriptor bit fields.
 *
 *   desc[7:0]    bti
 *   desc[13:8]   msg_ctrl (6 bits; HDC_RO only populates [10:8])
 *   desc[18:14]  msg_type (5 bits; values come from the SFID-specific
 *                GEN_DATAPORT_DC_* / GEN_DATAPORT_DC_PORT1_* ranges)
 */
typedef struct gen_hdc_desc {
   unsigned bti;
   unsigned msg_ctrl;
   unsigned msg_type;
} gen_hdc_desc;

/* Render-cache descriptor (pre-LSC).
 *
 *   desc[7:0]    bti
 *   desc[13:8]   msg_ctrl (6 bits; per-msg_type reinterpretation)
 *   desc[17:14]  msg_type (4 bits)
 *   desc[18]     coarse_write (rt_write only; gfx10+)
 */
typedef struct gen_render_desc {
   unsigned bti;
   unsigned msg_ctrl;
   unsigned msg_type;
   bool coarse_write;
} gen_render_desc;

/* LSC-specific surface bits of the extended descriptor.  The generic SEND
 * ex_mlen bits are handled separately.
 */
typedef struct gen_lsc_ex_desc {
   enum lsc_addr_surface_type addr_type;

   /* `base_offset` fields below hold a signed immediate offset in its
    * natural int form.  The encoder packs/truncates to an N-bit field
    * (LSC_ADDRESS_OFFSET_*_BITS in brw_eu.h: 20 for FLAT, 17 for BSS/SS,
    * 12 for BTI); the decoder sign-extends from N bits.
    */
   union {
      struct {
         int base_offset;
      } flat;

      struct {
         unsigned surface_state_index;
         /* Carried in ex_desc_imm_extra on Gfx20+. */
         int base_offset;
      } surface_state;

      struct {
         unsigned index;
         int base_offset;
      } bti;

      /* Block2d flat form: signed 10-bit immediate (x, y) offset added to
       * the block start in the address payload.  Element-coordinate units;
       * x * data-size bytes must be dword aligned.
       */
      struct {
         int x_off;
         int y_off;
      } block2d;
   };
} gen_lsc_ex_desc;

typedef struct gen_inst {
   gen_opcode opcode;

   uint8_t exec_size;

   gen_condition cmod;
   gen_predicate pred_control;
   uint8_t chan_offset;
   uint8_t flag_subnr;
   uint8_t flag_nr;
   uint8_t boolean_func_ctrl;  /* For BFN instruction. */
   uint8_t thread_control;

   bool align16:1;
   bool pred_inv:1;
   bool saturate:1;
   bool no_mask:1;
   bool branch_control:1;
   bool no_dd_clear:1;
   bool no_dd_check:1;
   bool atomic_control:1;
   bool debug_control:1;

   bool fusion_control:1;  /* Gfx12 only. */
   bool acc_wr_control:1;  /* Gfx12 only. */

   gen_swsb swsb;

   union {
      struct {
         gen_math func;
      } math;

      struct {
         gen_sync_func func;
      } sync;

      struct {
         uint8_t sdepth;
         uint8_t rcount;

         uint8_t src1_subbyte;
         uint8_t src2_subbyte;
      } dpas;

      struct {
         bool desc_is_reg:1;
         bool ex_desc_is_reg:1;
         bool ex_bso:1;
         bool eot:1;

         uint8_t ex_desc_subnr;
         gen_sfid sfid;
         uint8_t src1_len;

         uint32_t desc_imm;
         uint32_t ex_desc_imm;

         uint32_t ex_desc_imm_extra;  /* Gfx20+ only. */
      } send;
   };

   gen_operand dst;
   gen_operand src[3];
} gen_inst;

typedef struct gen_raw_inst {
   uint64_t data[2];
} gen_raw_inst;

typedef struct gen_raw_compact_inst {
   uint64_t data;
} gen_raw_compact_inst;

typedef struct gen_error {
   unsigned index;
   const char *msg;
} gen_error;

#ifdef __cplusplus
} /* extern "C" */
#endif
