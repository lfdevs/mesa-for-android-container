/*
 * Copyright © 2006 Intel Corporation
 * SPDX-License-Identifier: MIT
 *
 * Intel funded Tungsten Graphics to develop this 3D driver.
 * File originally authored by: Keith Whitwell <keithw@vmware.com>
 */

#pragma once

#include <stdbool.h>
#include <stdio.h>
#include "brw_compiler.h"
#include "brw_eu_defines.h"
#include "brw_isa_info.h"
#include "brw_reg.h"

#include "intel_wa.h"
#include "util/bitset.h"

#include "intel/compiler/gen/gen.h"

#ifdef __cplusplus
extern "C" {
#endif

bool brw_should_dump_shader_bin(void);
void brw_dump_shader_bin(void *assembly, int start_offset, int end_offset,
                         const char *identifier);

/* In Xe2+ each register is 64bytes/512bits long while older platforms it is
 * 32bytes/256bits long.
 */
static inline unsigned
reg_unit(const struct intel_device_info *devinfo)
{
   return devinfo->ver >= 20 ? 2 : 1;
}

/* Helpers for SEND instruction:
 */

/**
 * Construct a message descriptor immediate with the specified common
 * descriptor controls.
 */
static inline uint32_t
brw_message_desc(const struct intel_device_info *devinfo,
                 unsigned msg_length,
                 unsigned response_length,
                 bool header_present)
{
   assert(msg_length % reg_unit(devinfo) == 0);
   assert(response_length % reg_unit(devinfo) == 0);
   return (SET_BITS(msg_length / reg_unit(devinfo), 28, 25) |
           SET_BITS(response_length / reg_unit(devinfo), 24, 20) |
           SET_BITS(header_present, 19, 19));
}

static inline unsigned
brw_message_desc_mlen(const struct intel_device_info *devinfo, uint32_t desc)
{
   return GET_BITS(desc, 28, 25) * reg_unit(devinfo);
}

static inline unsigned
brw_message_desc_rlen(const struct intel_device_info *devinfo, uint32_t desc)
{
   return GET_BITS(desc, 24, 20) * reg_unit(devinfo);
}

static inline unsigned
brw_message_ex_desc(const struct intel_device_info *devinfo,
                    unsigned ex_msg_length)
{
   assert(ex_msg_length % reg_unit(devinfo) == 0);
   return devinfo->ver >= 20 ?
      SET_BITS(ex_msg_length / reg_unit(devinfo), 10, 6) :
      SET_BITS(ex_msg_length / reg_unit(devinfo), 9, 6);
}

static inline unsigned
brw_message_ex_desc_ex_mlen(const struct intel_device_info *devinfo,
                            uint32_t ex_desc)
{
   return devinfo->ver >= 20 ?
      GET_BITS(ex_desc, 10, 6) * reg_unit(devinfo) :
      GET_BITS(ex_desc, 9, 6) * reg_unit(devinfo);
}

static inline uint32_t
brw_urb_desc(const struct intel_device_info *devinfo,
             unsigned msg_type,
             bool per_slot_offset_present,
             bool channel_mask_present,
             unsigned global_offset)
{
   return (SET_BITS(per_slot_offset_present, 17, 17) |
           SET_BITS(channel_mask_present, 15, 15) |
           SET_BITS(global_offset, 14, 4) |
           SET_BITS(msg_type, 3, 0));
}

static inline uint32_t
brw_urb_desc_msg_type(ASSERTED const struct intel_device_info *devinfo,
                      uint32_t desc)
{
   return GET_BITS(desc, 3, 0);
}

static inline uint32_t
brw_urb_fence_desc(const struct intel_device_info *devinfo)
{
   assert(devinfo->has_lsc);
   return brw_urb_desc(devinfo, GEN_GFX125_URB_OPCODE_FENCE, false, false, 0);
}

/**
 * Construct a message descriptor immediate with the specified sampler
 * function controls.
 */
static inline uint32_t
brw_sampler_desc(const struct intel_device_info *devinfo,
                 unsigned binding_table_index,
                 unsigned sampler,
                 unsigned msg_type,
                 unsigned simd_mode,
                 unsigned return_format)
{
   const unsigned desc = (SET_BITS(binding_table_index, 7, 0) |
                          SET_BITS(sampler, 11, 8));

   /* From GFX20 Bspec: Shared Functions - Message Descriptor -
    * Sampling Engine:
    *
    *    Message Type[5]  31  This bit represents the upper bit of message type
    *                         6-bit encoding (c.f. [16:12]). This bit is set
    *                         for messages with programmable offsets.
    */
   if (devinfo->ver >= 20)
      return desc | SET_BITS(msg_type & 0x1F, 16, 12) |
             SET_BITS(simd_mode & 0x3, 18, 17) |
             SET_BITS(simd_mode >> 2, 29, 29) |
             SET_BITS(return_format, 30, 30) |
             SET_BITS(msg_type >> 5, 31, 31);

   /* From the CHV Bspec: Shared Functions - Message Descriptor -
    * Sampling Engine:
    *
    *   SIMD Mode[2]  29    This field is the upper bit of the 3-bit
    *                       SIMD Mode field.
    */
   return desc | SET_BITS(msg_type, 16, 12) |
          SET_BITS(simd_mode & 0x3, 18, 17) |
          SET_BITS(simd_mode >> 2, 29, 29) |
          SET_BITS(return_format, 30, 30);
}

static inline unsigned
brw_sampler_desc_binding_table_index(UNUSED
                                     const struct intel_device_info *devinfo,
                                     uint32_t desc)
{
   return GET_BITS(desc, 7, 0);
}

static inline unsigned
brw_sampler_desc_sampler(UNUSED const struct intel_device_info *devinfo,
                         uint32_t desc)
{
   return GET_BITS(desc, 11, 8);
}

static inline unsigned
brw_sampler_desc_msg_type(const struct intel_device_info *devinfo, uint32_t desc)
{
   if (devinfo->ver >= 20)
      return GET_BITS(desc, 31, 31) << 5 | GET_BITS(desc, 16, 12);
   else
      return GET_BITS(desc, 16, 12);
}

static inline unsigned
brw_sampler_desc_simd_mode(const struct intel_device_info *devinfo,
                           uint32_t desc)
{
   return GET_BITS(desc, 18, 17) | GET_BITS(desc, 29, 29) << 2;
}

static inline unsigned
brw_sampler_desc_return_format(ASSERTED const struct intel_device_info *devinfo,
                               uint32_t desc)
{
   return GET_BITS(desc, 30, 30);
}

/**
 * Construct a message descriptor for the dataport
 */
static inline uint32_t
brw_dp_desc(const struct intel_device_info *devinfo,
            unsigned binding_table_index,
            unsigned msg_type,
            unsigned msg_control)
{
   return SET_BITS(binding_table_index, 7, 0) |
          SET_BITS(msg_control, 13, 8) |
          SET_BITS(msg_type, 18, 14);
}

static inline unsigned
brw_dp_desc_binding_table_index(UNUSED const struct intel_device_info *devinfo,
                                uint32_t desc)
{
   return GET_BITS(desc, 7, 0);
}

static inline unsigned
brw_dp_desc_msg_type(const struct intel_device_info *devinfo, uint32_t desc)
{
   return GET_BITS(desc, 18, 14);
}

static inline unsigned
brw_dp_desc_msg_control(const struct intel_device_info *devinfo, uint32_t desc)
{
   return GET_BITS(desc, 13, 8);
}

/**
 * Construct a message descriptor immediate with the specified dataport read
 * function controls.
 */
static inline uint32_t
brw_dp_read_desc(const struct intel_device_info *devinfo,
                 unsigned binding_table_index,
                 unsigned msg_control,
                 unsigned msg_type,
                 unsigned target_cache)
{
   return brw_dp_desc(devinfo, binding_table_index, msg_type, msg_control);
}

static inline unsigned
brw_dp_read_desc_msg_control(const struct intel_device_info *devinfo,
                             uint32_t desc)
{
   return brw_dp_desc_msg_control(devinfo, desc);
}

/**
 * Construct a message descriptor immediate with the specified dataport write
 * function controls.
 */
static inline uint32_t
brw_dp_write_desc(const struct intel_device_info *devinfo,
                  unsigned binding_table_index,
                  unsigned msg_control,
                  unsigned msg_type,
                  unsigned send_commit_msg)
{
   assert(!send_commit_msg);
   return brw_dp_desc(devinfo, binding_table_index, msg_type, msg_control) |
          SET_BITS(send_commit_msg, 17, 17);
}

static inline unsigned
brw_dp_write_desc_msg_control(const struct intel_device_info *devinfo,
                              uint32_t desc)
{
   return brw_dp_desc_msg_control(devinfo, desc);
}

/**
 * Construct a message descriptor immediate with the specified dataport
 * surface function controls.
 */
static inline uint32_t
brw_dp_surface_desc(const struct intel_device_info *devinfo,
                    unsigned msg_type,
                    unsigned msg_control)
{
   /* We'll OR in the binding table index later */
   return brw_dp_desc(devinfo, 0, msg_type, msg_control);
}

static inline uint32_t
brw_dp_untyped_atomic_desc(const struct intel_device_info *devinfo,
                           unsigned exec_size, /**< 0 for SIMD4x2 */
                           unsigned atomic_op,
                           bool response_expected)
{
   assert(exec_size <= 8 || exec_size == 16);

   unsigned msg_type;
   if (exec_size > 0) {
      msg_type = GEN_DATAPORT_DC_PORT1_UNTYPED_ATOMIC_OP;
   } else {
      msg_type = GEN_DATAPORT_DC_PORT1_UNTYPED_ATOMIC_OP_SIMD4X2;
   }

   const unsigned msg_control =
      SET_BITS(atomic_op, 3, 0) |
      SET_BITS(0 < exec_size && exec_size <= 8, 4, 4) |
      SET_BITS(response_expected, 5, 5);

   return brw_dp_surface_desc(devinfo, msg_type, msg_control);
}

static inline uint32_t
brw_dp_untyped_atomic_float_desc(const struct intel_device_info *devinfo,
                                 unsigned exec_size,
                                 unsigned atomic_op,
                                 bool response_expected)
{
   assert(exec_size <= 8 || exec_size == 16);

   assert(exec_size > 0);
   const unsigned msg_type = GEN_DATAPORT_DC_PORT1_UNTYPED_ATOMIC_FLOAT_OP;

   const unsigned msg_control =
      SET_BITS(atomic_op, 1, 0) |
      SET_BITS(exec_size <= 8, 4, 4) |
      SET_BITS(response_expected, 5, 5);

   return brw_dp_surface_desc(devinfo, msg_type, msg_control);
}

static inline unsigned
brw_mdc_cmask(unsigned num_channels)
{
   /* See also MDC_CMASK in the SKL PRM Vol 2d. */
   return 0xf & (0xf << num_channels);
}

static inline uint32_t
brw_dp_untyped_surface_rw_desc(const struct intel_device_info *devinfo,
                               unsigned exec_size, /**< 0 for SIMD4x2 */
                               unsigned num_channels,
                               bool write)
{
   assert(exec_size <= 8 || exec_size == 16);

   const unsigned msg_type =
      write ? GEN_DATAPORT_DC_PORT1_UNTYPED_SURFACE_WRITE :
              GEN_DATAPORT_DC_PORT1_UNTYPED_SURFACE_READ;

   /* See also MDC_SM3 in the SKL PRM Vol 2d. */
   const unsigned simd_mode = exec_size == 0 ? 0 : /* SIMD4x2 */
                              exec_size <= 8 ? 2 : 1;

   const unsigned msg_control =
      SET_BITS(brw_mdc_cmask(num_channels), 3, 0) |
      SET_BITS(simd_mode, 5, 4);

   return brw_dp_surface_desc(devinfo, msg_type, msg_control);
}

static inline unsigned
brw_mdc_ds(unsigned bit_size)
{
   switch (bit_size) {
   case 8:
      return GFX7_BYTE_SCATTERED_DATA_ELEMENT_BYTE;
   case 16:
      return GFX7_BYTE_SCATTERED_DATA_ELEMENT_WORD;
   case 32:
      return GFX7_BYTE_SCATTERED_DATA_ELEMENT_DWORD;
   default:
      UNREACHABLE("Unsupported bit_size for byte scattered messages");
   }
}

static inline uint32_t
brw_dp_byte_scattered_rw_desc(const struct intel_device_info *devinfo,
                              unsigned exec_size,
                              unsigned bit_size,
                              bool write)
{
   assert(exec_size <= 8 || exec_size == 16);

   const unsigned msg_type =
      write ? HSW_DATAPORT_DC_PORT0_BYTE_SCATTERED_WRITE :
              HSW_DATAPORT_DC_PORT0_BYTE_SCATTERED_READ;

   assert(exec_size > 0);
   const unsigned msg_control =
      SET_BITS(exec_size == 16, 0, 0) |
      SET_BITS(brw_mdc_ds(bit_size), 3, 2);

   return brw_dp_surface_desc(devinfo, msg_type, msg_control);
}

static inline uint32_t
brw_dp_dword_scattered_rw_desc(const struct intel_device_info *devinfo,
                               unsigned exec_size,
                               bool write)
{
   assert(exec_size == 8 || exec_size == 16);

   const unsigned msg_type =
      write ? GFX6_DATAPORT_WRITE_MESSAGE_DWORD_SCATTERED_WRITE :
              GEN_DATAPORT_DC_DWORD_SCATTERED_READ;

   const unsigned msg_control =
      SET_BITS(1, 1, 1) | /* Legacy SIMD Mode */
      SET_BITS(exec_size == 16, 0, 0);

   return brw_dp_surface_desc(devinfo, msg_type, msg_control);
}

static inline uint32_t
brw_dp_oword_block_rw_desc(const struct intel_device_info *devinfo,
                           bool align_16B,
                           unsigned num_dwords,
                           bool write)
{
   /* Writes can only have addresses aligned by OWORDs (16 Bytes). */
   assert(!write || align_16B);

   const unsigned msg_type =
      write ?     GEN_DATAPORT_DC_OWORD_BLOCK_WRITE :
      align_16B ? GEN_DATAPORT_DC_OWORD_BLOCK_READ :
                  GEN_DATAPORT_DC_UNALIGNED_OWORD_BLOCK_READ;

   const unsigned msg_control =
      SET_BITS(BRW_DATAPORT_OWORD_BLOCK_DWORDS(num_dwords), 2, 0);

   return brw_dp_surface_desc(devinfo, msg_type, msg_control);
}

static inline uint32_t
brw_dp_a64_untyped_surface_rw_desc(const struct intel_device_info *devinfo,
                                   unsigned exec_size, /**< 0 for SIMD4x2 */
                                   unsigned num_channels,
                                   bool write)
{
   assert(exec_size <= 8 || exec_size == 16);

   unsigned msg_type =
      write ? GEN_DATAPORT_DC_PORT1_A64_UNTYPED_SURFACE_WRITE :
              GEN_DATAPORT_DC_PORT1_A64_UNTYPED_SURFACE_READ;

   /* See also MDC_SM3 in the SKL PRM Vol 2d. */
   const unsigned simd_mode = exec_size == 0 ? 0 : /* SIMD4x2 */
                              exec_size <= 8 ? 2 : 1;

   const unsigned msg_control =
      SET_BITS(brw_mdc_cmask(num_channels), 3, 0) |
      SET_BITS(simd_mode, 5, 4);

   return brw_dp_desc(devinfo, GEN_BTI_STATELESS_NON_COHERENT,
                      msg_type, msg_control);
}

static inline uint32_t
brw_dp_a64_oword_block_rw_desc(const struct intel_device_info *devinfo,
                               bool align_16B,
                               unsigned num_dwords,
                               bool write)
{
   /* Writes can only have addresses aligned by OWORDs (16 Bytes). */
   assert(!write || align_16B);

   unsigned msg_type =
      write ? GEN_DATAPORT_DC_PORT1_A64_OWORD_BLOCK_WRITE :
              GEN_DATAPORT_DC_PORT1_A64_OWORD_BLOCK_READ;

   unsigned msg_control =
      SET_BITS(!align_16B, 4, 3) |
      SET_BITS(BRW_DATAPORT_OWORD_BLOCK_DWORDS(num_dwords), 2, 0);

   return brw_dp_desc(devinfo, GEN_BTI_STATELESS_NON_COHERENT,
                      msg_type, msg_control);
}

/**
 * Calculate the data size (see MDC_A64_DS in the "Structures" volume of the
 * Skylake PRM).
 */
static inline uint32_t
brw_mdc_a64_ds(unsigned elems)
{
   switch (elems) {
   case 1:  return 0;
   case 2:  return 1;
   case 4:  return 2;
   case 8:  return 3;
   default:
      UNREACHABLE("Unsupported elmeent count for A64 scattered message");
   }
}

static inline uint32_t
brw_dp_a64_byte_scattered_rw_desc(const struct intel_device_info *devinfo,
                                  unsigned exec_size, /**< 0 for SIMD4x2 */
                                  unsigned bit_size,
                                  bool write)
{
   assert(exec_size <= 8 || exec_size == 16);

   unsigned msg_type =
      write ? GEN_DATAPORT_DC_PORT1_A64_SCATTERED_WRITE :
              GEN_DATAPORT_DC_PORT1_A64_SCATTERED_READ;

   const unsigned msg_control =
      SET_BITS(GFX8_A64_SCATTERED_SUBTYPE_BYTE, 1, 0) |
      SET_BITS(brw_mdc_a64_ds(bit_size / 8), 3, 2) |
      SET_BITS(exec_size == 16, 4, 4);

   return brw_dp_desc(devinfo, GEN_BTI_STATELESS_NON_COHERENT,
                      msg_type, msg_control);
}

static inline uint32_t
brw_dp_a64_untyped_atomic_desc(const struct intel_device_info *devinfo,
                               ASSERTED unsigned exec_size, /**< 0 for SIMD4x2 */
                               unsigned bit_size,
                               unsigned atomic_op,
                               bool response_expected)
{
   assert(exec_size == 8);
   assert(bit_size == 16 || bit_size == 32 || bit_size == 64);
   assert(devinfo->ver >= 12 || bit_size >= 32);

   const unsigned msg_type = bit_size == 16 ?
      GEN_GFX12_DATAPORT_DC_PORT1_A64_UNTYPED_ATOMIC_HALF_INT_OP :
      GEN_DATAPORT_DC_PORT1_A64_UNTYPED_ATOMIC_OP;

   const unsigned msg_control =
      SET_BITS(atomic_op, 3, 0) |
      SET_BITS(bit_size == 64, 4, 4) |
      SET_BITS(response_expected, 5, 5);

   return brw_dp_desc(devinfo, GEN_BTI_STATELESS_NON_COHERENT,
                      msg_type, msg_control);
}

static inline uint32_t
brw_dp_a64_untyped_atomic_float_desc(const struct intel_device_info *devinfo,
                                     ASSERTED unsigned exec_size,
                                     unsigned bit_size,
                                     unsigned atomic_op,
                                     bool response_expected)
{
   assert(exec_size == 8);
   assert(bit_size == 16 || bit_size == 32);
   assert(devinfo->ver >= 12 || bit_size == 32);

   assert(exec_size > 0);
   const unsigned msg_type = bit_size == 32 ?
      GEN_DATAPORT_DC_PORT1_A64_UNTYPED_ATOMIC_FLOAT_OP :
      GEN_GFX12_DATAPORT_DC_PORT1_A64_UNTYPED_ATOMIC_HALF_FLOAT_OP;

   const unsigned msg_control =
      SET_BITS(atomic_op, 1, 0) |
      SET_BITS(response_expected, 5, 5);

   return brw_dp_desc(devinfo, GEN_BTI_STATELESS_NON_COHERENT,
                      msg_type, msg_control);
}

static inline uint32_t
brw_dp_typed_atomic_desc(const struct intel_device_info *devinfo,
                         unsigned exec_size,
                         unsigned exec_group,
                         unsigned atomic_op,
                         bool response_expected)
{
   assert(exec_size > 0 || exec_group == 0);
   assert(exec_group % 8 == 0);

   const unsigned msg_type =
      exec_size == 0 ? GEN_DATAPORT_DC_PORT1_TYPED_ATOMIC_OP_SIMD4X2 :
                       GEN_DATAPORT_DC_PORT1_TYPED_ATOMIC_OP;

   const bool high_sample_mask = (exec_group / 8) % 2 == 1;

   const unsigned msg_control =
      SET_BITS(atomic_op, 3, 0) |
      SET_BITS(high_sample_mask, 4, 4) |
      SET_BITS(response_expected, 5, 5);

   return brw_dp_surface_desc(devinfo, msg_type, msg_control);
}

static inline uint32_t
brw_dp_typed_surface_rw_desc(const struct intel_device_info *devinfo,
                             unsigned exec_size,
                             unsigned exec_group,
                             unsigned num_channels,
                             bool write)
{
   assert(exec_size > 0 || exec_group == 0);
   assert(exec_group % 8 == 0);

   /* Typed surface reads and writes don't support SIMD16 */
   assert(exec_size <= 8);

   const unsigned msg_type =
      write ? GEN_DATAPORT_DC_PORT1_TYPED_SURFACE_WRITE :
              GEN_DATAPORT_DC_PORT1_TYPED_SURFACE_READ;

   /* See also MDC_SG3 in the SKL PRM Vol 2d. */
   const unsigned slot_group = exec_size == 0 ? 0 : /* SIMD4x2 */
                               1 + ((exec_group / 8) % 2);

   const unsigned msg_control =
      SET_BITS(brw_mdc_cmask(num_channels), 3, 0) |
      SET_BITS(slot_group, 5, 4);

   return brw_dp_surface_desc(devinfo, msg_type, msg_control);
}

static inline uint32_t
brw_fb_desc(const struct intel_device_info *devinfo,
            unsigned binding_table_index,
            unsigned msg_type,
            unsigned msg_control)
{
   return SET_BITS(binding_table_index, 7, 0) |
          SET_BITS(msg_control, 13, 8) |
          SET_BITS(msg_type, 17, 14);
}

static inline unsigned
brw_fb_desc_binding_table_index(UNUSED const struct intel_device_info *devinfo,
                                uint32_t desc)
{
   return GET_BITS(desc, 7, 0);
}

static inline uint32_t
brw_fb_desc_msg_control(const struct intel_device_info *devinfo, uint32_t desc)
{
   return GET_BITS(desc, 13, 8);
}

static inline unsigned
brw_fb_desc_msg_type(const struct intel_device_info *devinfo, uint32_t desc)
{
   return GET_BITS(desc, 17, 14);
}

static inline uint32_t
brw_fb_read_desc(const struct intel_device_info *devinfo,
                 unsigned binding_table_index,
                 unsigned msg_control,
                 unsigned exec_size,
                 bool per_sample)
{
   assert(exec_size == 8 || exec_size == 16);

   return brw_fb_desc(devinfo, binding_table_index,
                      GEN_DATAPORT_RC_RENDER_TARGET_READ, msg_control) |
          SET_BITS(per_sample, 13, 13) |
          SET_BITS(exec_size == 8, 8, 8) /* Render Target Message Subtype */;
}

static inline uint32_t
brw_fb_write_desc(const struct intel_device_info *devinfo,
                  unsigned binding_table_index,
                  unsigned msg_control,
                  bool last_render_target,
                  bool coarse_write)
{
   const unsigned msg_type = GEN_DATAPORT_WRITE_MESSAGE_RENDER_TARGET_WRITE;

   assert(devinfo->ver >= 10 || !coarse_write);

   return brw_fb_desc(devinfo, binding_table_index, msg_type, msg_control) |
          SET_BITS(last_render_target, 12, 12) |
          SET_BITS(coarse_write, 18, 18);
}

static inline bool
brw_fb_write_desc_last_render_target(const struct intel_device_info *devinfo,
                                     uint32_t desc)
{
   return GET_BITS(desc, 12, 12);
}

static inline bool
brw_fb_write_desc_coarse_write(const struct intel_device_info *devinfo,
                               uint32_t desc)
{
   assert(devinfo->ver >= 10);
   return GET_BITS(desc, 18, 18);
}

static inline unsigned
brw_lsc_op_to_legacy_atomic(unsigned _op)
{
   enum lsc_opcode op = (enum lsc_opcode) _op;

   switch (op) {
   case LSC_OP_ATOMIC_INC:
      return GEN_AOP_INC;
   case LSC_OP_ATOMIC_DEC:
      return GEN_AOP_DEC;
   case LSC_OP_ATOMIC_STORE:
      return GEN_AOP_MOV;
   case LSC_OP_ATOMIC_ADD:
      return GEN_AOP_ADD;
   case LSC_OP_ATOMIC_SUB:
      return GEN_AOP_SUB;
   case LSC_OP_ATOMIC_MIN:
      return GEN_AOP_IMIN;
   case LSC_OP_ATOMIC_MAX:
      return GEN_AOP_IMAX;
   case LSC_OP_ATOMIC_UMIN:
      return GEN_AOP_UMIN;
   case LSC_OP_ATOMIC_UMAX:
      return GEN_AOP_UMAX;
   case LSC_OP_ATOMIC_CMPXCHG:
      return GEN_AOP_CMPWR;
   case LSC_OP_ATOMIC_FADD:
      return GEN_AOP_FADD;
   case LSC_OP_ATOMIC_FMIN:
      return GEN_AOP_FMIN;
   case LSC_OP_ATOMIC_FMAX:
      return GEN_AOP_FMAX;
   case LSC_OP_ATOMIC_FCMPXCHG:
      return GEN_AOP_FCMPWR;
   case LSC_OP_ATOMIC_AND:
      return GEN_AOP_AND;
   case LSC_OP_ATOMIC_OR:
      return GEN_AOP_OR;
   case LSC_OP_ATOMIC_XOR:
      return GEN_AOP_XOR;
   /* No LSC op maps to GEN_AOP_PREDEC */
   case LSC_OP_ATOMIC_LOAD:
   case LSC_OP_ATOMIC_FSUB:
      UNREACHABLE("no corresponding legacy atomic operation");
   case LSC_OP_LOAD:
   case LSC_OP_LOAD_CMASK:
   case LSC_OP_LOAD_2D_BLOCK:
   case LSC_OP_STORE:
   case LSC_OP_STORE_CMASK:
   case LSC_OP_STORE_2D_BLOCK:
   case LSC_OP_FENCE:
   case LSC_OP_LOAD_CMASK_MSRT:
   case LSC_OP_STORE_CMASK_MSRT:
      UNREACHABLE("not an atomic op");
   }

   UNREACHABLE("invalid LSC op");
}

static inline unsigned
brw_lsc_msg_dest_len(const struct intel_device_info *devinfo,
                     enum lsc_data_size data_sz, unsigned n)
{
   return DIV_ROUND_UP(lsc_data_size_bytes(data_sz) * n,
                       reg_unit(devinfo) * REG_SIZE) * reg_unit(devinfo);
}

static inline unsigned
brw_lsc_msg_addr_len(const struct intel_device_info *devinfo,
                     enum lsc_addr_size addr_sz, unsigned n)
{
   return DIV_ROUND_UP(lsc_addr_size_bytes(addr_sz) * n,
                       reg_unit(devinfo) * REG_SIZE) * reg_unit(devinfo);
}

static inline uint32_t
brw_mdc_sm2(unsigned exec_size)
{
   assert(exec_size == 8 || exec_size == 16);
   return exec_size > 8;
}

static inline uint32_t
brw_mdc_sm2_exec_size(uint32_t sm2)
{
   assert(sm2 <= 1);
   return 8 << sm2;
}

static inline uint32_t
brw_btd_spawn_desc(ASSERTED const struct intel_device_info *devinfo,
                   unsigned exec_size, unsigned msg_type)
{
   assert(devinfo->has_ray_tracing);
   assert(devinfo->ver < 20 || exec_size == 16);

   return SET_BITS(0, 19, 19) | /* No header */
          SET_BITS(msg_type, 17, 14) |
          SET_BITS(brw_mdc_sm2(exec_size), 8, 8);
}

static inline uint32_t
brw_btd_spawn_msg_type(UNUSED const struct intel_device_info *devinfo,
                       uint32_t desc)
{
   return GET_BITS(desc, 17, 14);
}

static inline uint32_t
brw_btd_spawn_exec_size(UNUSED const struct intel_device_info *devinfo,
                        uint32_t desc)
{
   return brw_mdc_sm2_exec_size(GET_BITS(desc, 8, 8));
}

static inline uint32_t
brw_rt_trace_ray_desc(ASSERTED const struct intel_device_info *devinfo,
                      unsigned exec_size)
{
   assert(devinfo->has_ray_tracing);
   assert(devinfo->ver < 20 || exec_size == 16);

   return SET_BITS(0, 19, 19) | /* No header */
          SET_BITS(0, 17, 14) | /* Message type */
          SET_BITS(brw_mdc_sm2(exec_size), 8, 8);
}

static inline uint32_t
brw_rt_trace_ray_desc_exec_size(UNUSED const struct intel_device_info *devinfo,
                                uint32_t desc)
{
   return brw_mdc_sm2_exec_size(GET_BITS(desc, 8, 8));
}

/**
 * Construct a message descriptor immediate with the specified pixel
 * interpolator function controls.
 */
static inline uint32_t
brw_pixel_interp_desc(UNUSED const struct intel_device_info *devinfo,
                      unsigned msg_type,
                      bool noperspective,
                      bool coarse_pixel_rate,
                      unsigned exec_size,
                      unsigned group)
{
   assert(exec_size == 8 || exec_size == 16);
   const bool simd_mode = exec_size == 16;
   const bool slot_group = group >= 16;

   assert(devinfo->ver >= 10 || !coarse_pixel_rate);
   return (SET_BITS(slot_group, 11, 11) |
           SET_BITS(msg_type, 13, 12) |
           SET_BITS(!!noperspective, 14, 14) |
           SET_BITS(coarse_pixel_rate, 15, 15) |
           SET_BITS(simd_mode, 16, 16));
}

enum brw_conditional_mod brw_negate_cmod(enum brw_conditional_mod cmod);
enum brw_conditional_mod brw_swap_cmod(enum brw_conditional_mod cmod);

/** Maximum SEND message length */
#define BRW_MAX_MSG_LENGTH 15

/** Offset encoding signed size limits (top bit is the sign) */
#define LSC_ADDRESS_OFFSET_FLAT_BITS 20
#define LSC_ADDRESS_OFFSET_SS_BITS   17
#define LSC_ADDRESS_OFFSET_BTI_BITS  12

static inline unsigned
brw_max_immediate_offset_bits(enum lsc_addr_surface_type binding_type)
{
   static const unsigned max_bits[] = {
      [LSC_ADDR_SURFTYPE_FLAT] = LSC_ADDRESS_OFFSET_FLAT_BITS,
      [LSC_ADDR_SURFTYPE_BSS]  = LSC_ADDRESS_OFFSET_SS_BITS,
      [LSC_ADDR_SURFTYPE_SS]   = LSC_ADDRESS_OFFSET_SS_BITS,
      [LSC_ADDR_SURFTYPE_BTI]  = LSC_ADDRESS_OFFSET_BTI_BITS,
   };
   assert(binding_type <= LSC_ADDR_SURFTYPE_BTI);
   return max_bits[binding_type];
}

static inline bool
brw_lsc_supports_base_offset(const struct intel_device_info *devinfo)
{
   return devinfo->ver >= 20;
}

static inline bool
brw_can_coherent_fb_fetch(const struct intel_device_info *devinfo)
{
   /* Not functional after Gfx20 */
   return devinfo->ver >= 9 && devinfo->ver < 20;
}

#ifdef __cplusplus
}
#endif
