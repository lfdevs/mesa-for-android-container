/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#include "jay_ir.h"

static enum jay_stride
min_stride_for_type(enum jay_type T)
{
   unsigned bits = jay_type_size_bits(T);

   /* We need at least enough contiguous bits per-lane to store a scalar */
   if (bits == 64)
      return JAY_STRIDE_8;
   else if (bits == 32)
      return JAY_STRIDE_4;
   else
      return JAY_STRIDE_2;
}

static enum jay_stride
max_stride_for_type(enum jay_type T)
{
   /* Horizontal stride can be at most 4 */
   return (jay_type_size_bits(T) >= 16) ? JAY_STRIDE_8 : JAY_STRIDE_4;
}

static bool
restrict_mixed_strides(jay_inst *I, unsigned s)
{
   /* From the hardware spec section "Register Region Restrictions":
    *
    * "In case of all floating point data types used in destination:" and
    *
    * "In case where source or destination datatype is 64b or operation is
    *  integer DWord multiply:" and
    *
    *  "Src2 Restrictions"
    *
    *      Register Regioning patterns where register data bit location
    *      of the LSB of the channels are changed between source and
    *      destination are not supported on Src0 and Src1 except for
    *      broadcast of a scalar.
    *
    * Therefore, ban mixed-strides in these cases.
    *
    * Similarly, SENDs cannot do any regioning so restrict that too.
    */
   return jay_type_is_any_float(I->type) ||
          jay_type_size_bits(I->type) == 64 ||
          jay_is_send_like(I) ||
          I->op == JAY_OPCODE_MUL_32X16 ||
          I->op == JAY_OPCODE_MUL_32 ||
          s == 2;
}

enum jay_stride
jay_dst_stride_minmax(jay_inst *I, bool do_max)
{
   enum jay_stride min = min_stride_for_type(I->type);
   enum jay_stride max = max_stride_for_type(I->type);

   /* Destination stride must be equal to the ratio of the sizes of the
    * execution data type to the destination type
    */
   if (I->op == JAY_OPCODE_CVT) {
      min = MAX2(min, min_stride_for_type(jay_src_type(I, 0)));

      /* Conversions between integer and HF must be strided by a DWord on the
       * destination.
       */
      if ((I->type == JAY_TYPE_F16 &&
           !jay_type_is_any_float(jay_cvt_src_type(I))) ||
          (jay_cvt_src_type(I) == JAY_TYPE_F16 &&
           !jay_type_is_any_float(I->type)))
         min = JAY_STRIDE_4;
   }

   if (I->op == JAY_OPCODE_EXPAND_QUAD) {
      return JAY_STRIDE_4;
   }

   /* V/UV types are restricted */
   if (I->op == JAY_OPCODE_SHR_ODD_SUBSPANS_BY_4) {
      return JAY_STRIDE_2;
   }

   if (I->op == JAY_OPCODE_SLICE_REPACK && jay_slice_repack_unpack(I)) {
      return JAY_STRIDE_4;
   }

   /* The src2 restriction quoted above effectively implies we should not stride
    * destinations of 3-source instructions either.
    */
   if (jay_num_isa_srcs(I) >= 3) {
      return min;
   }

   return (do_max && !restrict_mixed_strides(I, 0)) ? max : min;
}

enum jay_stride
jay_src_stride_minmax(jay_inst *I, unsigned s, bool do_max)
{
   enum jay_stride min = min_stride_for_type(jay_src_type(I, s));
   enum jay_stride max = max_stride_for_type(jay_src_type(I, s));

   /* BSpec 56640: bfloat sources must be packed */
   if (jay_src_type(I, s) == JAY_TYPE_BF16) {
      return JAY_STRIDE_2;
   }

   /* SENDs cannot do any regioning so force exactly the types of the sources
    * regardless of the type of the destination.
    *
    * Shuffles could theoretically support regioning but it would be nontrivial
    * and probably pointless most of the time.
    */
   if (jay_is_send_like(I) || jay_is_shuffle_like(I)) {
      return min;
   }

   /* While "add.u16 r0<2>, r1<4>" is legal, "add.u16 r0, r1<4>" is not.
    * Conservatively assume the destination is packed and restrict the source
    * stride accordingly. This satisfies the special restrictions.
    */
   if (jay_type_size_bits(I->type) <= 16) {
      max = JAY_STRIDE_4;
   }

   if (restrict_mixed_strides(I, s))
      return jay_dst_stride_minmax(I, do_max);

   return (do_max && !restrict_mixed_strides(I, s)) ? max : min;
}
