/**************************************************************************
 *
 * Copyright 2010 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 **************************************************************************/


#include "util/u_debug.h"
#include "util/u_cpu_detect.h"
#include "lp_bld_debug.h"
#include "lp_bld_const.h"
#include "lp_bld_format.h"
#include "lp_bld_gather.h"
#include "lp_bld_swizzle.h"
#include "lp_bld_init.h"
#include "lp_bld_intr.h"


/**
 * Get the pointer to one element from scatter positions in memory.
 *
 * @sa lp_build_gather()
 */
LLVMValueRef
lp_build_gather_elem_ptr(struct gallivm_state *gallivm,
                         unsigned length,
                         LLVMValueRef base_ptr,
                         LLVMValueRef offsets,
                         unsigned i)
{
   LLVMValueRef offset;
   LLVMValueRef ptr;

   assert(LLVMTypeOf(base_ptr) == LLVMPointerType(LLVMInt8TypeInContext(gallivm->context), 0));

   if (length == 1) {
      assert(i == 0);
      offset = offsets;
   } else {
      LLVMValueRef index = lp_build_const_int32(gallivm, i);
      offset = LLVMBuildExtractElement(gallivm->builder, offsets, index, "");
   }

   ptr = LLVMBuildGEP(gallivm->builder, base_ptr, &offset, 1, "");

   return ptr;
}


/**
 * Gather one element from scatter positions in memory.
 *
 * @sa lp_build_gather()
 */
LLVMValueRef
lp_build_gather_elem(struct gallivm_state *gallivm,
                     unsigned length,
                     unsigned src_width,
                     unsigned dst_width,
                     boolean aligned,
                     LLVMValueRef base_ptr,
                     LLVMValueRef offsets,
                     unsigned i,
                     boolean vector_justify)
{
   LLVMTypeRef src_type = LLVMIntTypeInContext(gallivm->context, src_width);
   LLVMTypeRef src_ptr_type = LLVMPointerType(src_type, 0);
   LLVMTypeRef dst_elem_type = LLVMIntTypeInContext(gallivm->context, dst_width);
   LLVMValueRef ptr;
   LLVMValueRef res;

   assert(LLVMTypeOf(base_ptr) == LLVMPointerType(LLVMInt8TypeInContext(gallivm->context), 0));

   ptr = lp_build_gather_elem_ptr(gallivm, length, base_ptr, offsets, i);
   ptr = LLVMBuildBitCast(gallivm->builder, ptr, src_ptr_type, "");
   res = LLVMBuildLoad(gallivm->builder, ptr, "");

   /* XXX
    * On some archs we probably really want to avoid having to deal
    * with alignments lower than 4 bytes (if fetch size is a power of
    * two >= 32). On x86 it doesn't matter, however.
    * We should be able to guarantee full alignment for any kind of texture
    * fetch (except ARB_texture_buffer_range, oops), but not vertex fetch
    * (there's PIPE_CAP_VERTEX_BUFFER_OFFSET_4BYTE_ALIGNED_ONLY and friends
    * but I don't think that's quite what we wanted).
    * For ARB_texture_buffer_range, PIPE_CAP_TEXTURE_BUFFER_OFFSET_ALIGNMENT
    * looks like a good fit, but it seems this cap bit (and OpenGL) aren't
    * enforcing what we want (which is what d3d10 does, the offset needs to
    * be aligned to element size, but GL has bytes regardless of element
    * size which would only leave us with minimum alignment restriction of 16
    * which doesn't make much sense if the type isn't 4x32bit). Due to
    * translation of offsets to first_elem in sampler_views it actually seems
    * gallium could not do anything else except 16 no matter what...
    */
  if (!aligned) {
      LLVMSetAlignment(res, 1);
   }

   assert(src_width <= dst_width);
   if (src_width > dst_width) {
      res = LLVMBuildTrunc(gallivm->builder, res, dst_elem_type, "");
   } else if (src_width < dst_width) {
      res = LLVMBuildZExt(gallivm->builder, res, dst_elem_type, "");
      if (vector_justify) {
#ifdef PIPE_ARCH_BIG_ENDIAN
         res = LLVMBuildShl(gallivm->builder, res,
                            LLVMConstInt(dst_elem_type, dst_width - src_width, 0), "");
#endif
      }
   }

   return res;
}


static LLVMValueRef
lp_build_gather_avx2(struct gallivm_state *gallivm,
                     unsigned length,
                     unsigned src_width,
                     unsigned dst_width,
                     LLVMValueRef base_ptr,
                     LLVMValueRef offsets)
{
   LLVMBuilderRef builder = gallivm->builder;
   LLVMTypeRef dst_type = LLVMIntTypeInContext(gallivm->context, dst_width);
   LLVMTypeRef dst_vec_type = LLVMVectorType(dst_type, length);
   LLVMTypeRef src_type = LLVMIntTypeInContext(gallivm->context, src_width);
   LLVMTypeRef src_vec_type = LLVMVectorType(src_type, length);
   LLVMValueRef res;

   assert(LLVMTypeOf(base_ptr) == LLVMPointerType(LLVMInt8TypeInContext(gallivm->context), 0));

   if (0) {
      /*
       * XXX: This will cause LLVM pre 3.7 to hang; it works on LLVM 3.8 but
       * will not use the AVX2 gather instrinsics.  See
       * http://lists.llvm.org/pipermail/llvm-dev/2016-January/094448.html
       */
      LLVMTypeRef i32_type = LLVMIntTypeInContext(gallivm->context, 32);
      LLVMTypeRef i32_vec_type = LLVMVectorType(i32_type, length);
      LLVMTypeRef i1_type = LLVMIntTypeInContext(gallivm->context, 1);
      LLVMTypeRef i1_vec_type = LLVMVectorType(i1_type, length);
      LLVMTypeRef src_ptr_type = LLVMPointerType(src_type, 0);
      LLVMValueRef src_ptr;

      base_ptr = LLVMBuildBitCast(builder, base_ptr, src_ptr_type, "");

      /* Rescale offsets from bytes to elements */
      LLVMValueRef scale = LLVMConstInt(i32_type, src_width/8, 0);
      scale = lp_build_broadcast(gallivm, i32_vec_type, scale);
      assert(LLVMTypeOf(offsets) == i32_vec_type);
      offsets = LLVMBuildSDiv(builder, offsets, scale, "");

      src_ptr = LLVMBuildGEP(builder, base_ptr, &offsets, 1, "vector-gep");

      char intrinsic[64];
      util_snprintf(intrinsic, sizeof intrinsic, "llvm.masked.gather.v%ui%u", length, src_width);
      LLVMValueRef alignment = LLVMConstInt(i32_type, src_width/8, 0);
      LLVMValueRef mask = LLVMConstAllOnes(i1_vec_type);
      LLVMValueRef passthru = LLVMGetUndef(src_vec_type);

      LLVMValueRef args[] = { src_ptr, alignment, mask, passthru };

      res = lp_build_intrinsic(builder, intrinsic, src_vec_type, args, 4, 0);
   } else {
      assert(src_width == 32);

      LLVMTypeRef i8_type = LLVMIntTypeInContext(gallivm->context, 8);

      /*
       * We should get the caller to give more type information so we can use
       * the intrinsics for the right int/float domain.  Int should be the most
       * common.
       */
      const char *intrinsic = NULL;
      switch (length) {
      case 4:
         intrinsic = "llvm.x86.avx2.gather.d.d";
         break;
      case 8:
         intrinsic = "llvm.x86.avx2.gather.d.d.256";
         break;
      default:
         assert(0);
      }

      LLVMValueRef passthru = LLVMGetUndef(src_vec_type);
      LLVMValueRef mask = LLVMConstAllOnes(src_vec_type);
      mask = LLVMConstBitCast(mask, src_vec_type);
      LLVMValueRef scale = LLVMConstInt(i8_type, 1, 0);

      LLVMValueRef args[] = { passthru, base_ptr, offsets, mask, scale };

      res = lp_build_intrinsic(builder, intrinsic, src_vec_type, args, 5, 0);
   }

   if (src_width > dst_width) {
      res = LLVMBuildTrunc(builder, res, dst_vec_type, "");
   } else if (src_width < dst_width) {
      res = LLVMBuildZExt(builder, res, dst_vec_type, "");
   }

   return res;
}


/**
 * Gather elements from scatter positions in memory into a single vector.
 * Use for fetching texels from a texture.
 * For SSE, typical values are length=4, src_width=32, dst_width=32.
 *
 * When src_width < dst_width, the return value can be justified in
 * one of two ways:
 * "integer justification" is used when the caller treats the destination
 * as a packed integer bitmask, as described by the channels' "shift" and
 * "width" fields;
 * "vector justification" is used when the caller casts the destination
 * to a vector and needs channel X to be in vector element 0.
 *
 * @param length length of the offsets
 * @param src_width src element width in bits
 * @param dst_width result element width in bits (src will be expanded to fit)
 * @param aligned whether the data is guaranteed to be aligned (to src_width)
 * @param base_ptr base pointer, should be a i8 pointer type.
 * @param offsets vector with offsets
 * @param vector_justify select vector rather than integer justification
 */
LLVMValueRef
lp_build_gather(struct gallivm_state *gallivm,
                unsigned length,
                unsigned src_width,
                unsigned dst_width,
                boolean aligned,
                LLVMValueRef base_ptr,
                LLVMValueRef offsets,
                boolean vector_justify)
{
   LLVMValueRef res;

   if (length == 1) {
      /* Scalar */
      return lp_build_gather_elem(gallivm, length,
                                  src_width, dst_width, aligned,
                                  base_ptr, offsets, 0, vector_justify);
   } else if (util_cpu_caps.has_avx2 && src_width == 32 && (length == 4 || length == 8)) {
      return lp_build_gather_avx2(gallivm, length, src_width, dst_width, base_ptr, offsets);
   } else {
      /* Vector */

      LLVMTypeRef dst_elem_type = LLVMIntTypeInContext(gallivm->context, dst_width);
      LLVMTypeRef dst_vec_type = LLVMVectorType(dst_elem_type, length);
      unsigned i;

      res = LLVMGetUndef(dst_vec_type);
      for (i = 0; i < length; ++i) {
         LLVMValueRef index = lp_build_const_int32(gallivm, i);
         LLVMValueRef elem;
         elem = lp_build_gather_elem(gallivm, length,
                                     src_width, dst_width, aligned,
                                     base_ptr, offsets, i, vector_justify);
         res = LLVMBuildInsertElement(gallivm->builder, res, elem, index, "");
      }
   }

   return res;
}

LLVMValueRef
lp_build_gather_values(struct gallivm_state * gallivm,
                       LLVMValueRef * values,
                       unsigned value_count)
{
   LLVMTypeRef vec_type = LLVMVectorType(LLVMTypeOf(values[0]), value_count);
   LLVMBuilderRef builder = gallivm->builder;
   LLVMValueRef vec = LLVMGetUndef(vec_type);
   unsigned i;

   for (i = 0; i < value_count; i++) {
      LLVMValueRef index = lp_build_const_int32(gallivm, i);
      vec = LLVMBuildInsertElement(builder, vec, values[i], index, "");
   }
   return vec;
}
