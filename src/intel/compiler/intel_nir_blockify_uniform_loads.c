/*
 * Copyright © 2018 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "dev/intel_device_info.h"
#include "intel_nir.h"
#include "isl/isl.h"
#include "nir_builder.h"

static bool
rebase_const_offset_ubo_loads_intr(nir_builder *b,
                                   nir_intrinsic_instr *intrin,
                                   void *cb_data)
{
   if (intrin->intrinsic != nir_intrinsic_load_ubo_uniform_block_intel)
      return false;

   if (!nir_src_is_const(intrin->src[1]))
      return false;

   const unsigned type_bytes = intrin->def.bit_size / 8;
   const unsigned cacheline_bytes = 64;
   const unsigned block_components =
      MIN2(cacheline_bytes / type_bytes, NIR_MAX_VEC_COMPONENTS);

   const unsigned orig_offset = nir_src_as_uint(intrin->src[1]);
   const unsigned new_offset = ROUND_DOWN_TO(orig_offset, cacheline_bytes);

   const unsigned orig_def_components = intrin->def.num_components;
   const unsigned orig_read_components =
      nir_def_last_component_read(&intrin->def) + 1;
   const unsigned pad_components = (orig_offset - new_offset) / type_bytes;

   /* Don't round down if we'd have to split a single load into two loads */
   if (orig_read_components + pad_components > block_components)
      return false;

   /* Always read a full block so we can CSE reads of different sizes.
    * The backend will skip reading unused trailing components anyway.
    */
   intrin->def.num_components = block_components;
   intrin->num_components = block_components;
   nir_intrinsic_set_range(intrin, block_components * type_bytes);
   nir_intrinsic_set_align_offset(intrin, 0);

   /* We're running this pass before the constant offset extraction, so it
    * should be 0 at this point, otherwise some other pass modified this value
    * and likely didn't teak into account our HW requirements.
    */
   assert(nir_intrinsic_base(intrin) == 0);

   if (pad_components) {
      /* Change the base of the load to the new lower offset, and emit
       * moves to read from the now higher vector component locations.
       */
      b->cursor = nir_before_instr(&intrin->instr);
      nir_src_rewrite(&intrin->src[1], nir_imm_int(b, new_offset));
   }

   b->cursor = nir_after_instr(&intrin->instr);

   nir_scalar components[NIR_MAX_VEC_COMPONENTS];
   nir_scalar undef = nir_get_scalar(nir_undef(b, 1, type_bytes * 8), 0);
   unsigned i = 0;
   for (; i < orig_read_components; i++)
      components[i] = nir_get_scalar(&intrin->def, pad_components + i);
   for (; i < orig_def_components; i++)
      components[i] = undef;

   nir_def *rebase = nir_vec_scalars(b, components, orig_def_components);
   rebase->divergent = false;

   nir_def_rewrite_uses_after(&intrin->def, rebase);
   return true;
}

/**
 * Shaders commonly contain small UBO loads with a constant offset scattered
 * throughout the program.  Ideally, we want to vectorize those into larger
 * block loads so we can load whole cachelines at a time, or at least fill
 * whole 32B registers rather than having empty space.
 *
 * nir_opt_load_store_vectorize() is terrific for combining small loads into
 * nice large block loads.  Unfortunately, it only vectorizes within a single
 * basic block, and there's a lot of opportunity for optimizing globally.
 *
 * In the past, our backend loaded whole 64B cachelines at a time (on pre-Xe2,
 * two registers) and rounded down constant UBO load offsets to the nearest
 * multiple of 64B.  This meant multiple loads within the same 64B would be
 * CSE'd into the same load, and we could even take advantage of global CSE.
 * However, we didn't have a method for shrinking loads from 64B back to 32B
 * again, and also didn't have a lot of flexibility in how this interacted
 * with the NIR load/store vectorization.
 *
 * This pass takes a similar approach, but in NIR.  The idea is to:
 *
 * 1. Run load/store vectorization to combine access within a basic block
 *
 * 2. Find load_ubo_uniform_block_intel intrinsics with constant offsets.
 *    Round their base down to the nearest multiple of 64B, and also increase
 *    their returned vector to be a vec16 (64B for 32-bit values).  However,
 *    only do this if a single vec16 load would cover this additional "pad"
 *    space at the front, and all used components of the existing load.  That
 *    way, we don't blindly turn a single load into two loads.
 *
 *    If we made any progress, then...
 *
 * 3. Run global CSE.  This will coalesce any accesses to the same 64B
 *    region across subtrees of the CFG.
 *
 * 4. Run the load/store vectorizer again for UBOs.  This will clean up
 *    any overlapping memory access within a block.
 *
 * 5. Have the backend only issue loads for components of the vec16 which
 *    are actually read.  We could also shrink this in NIR, but doing it in
 *    the backend is pretty straightforward.
 *
 * We could probably do better with a fancier sliding-window type pass
 * which looked across blocks to produce optimal loads.  However, this
 * simple hack using existing passes does a fairly good job for now.
 */
bool
brw_nir_rebase_const_offset_ubo_loads(nir_shader *shader)
{
   return nir_shader_intrinsics_pass(shader,
                                     rebase_const_offset_ubo_loads_intr,
                                     nir_metadata_control_flow |
                                     nir_metadata_live_defs,
                                     NULL);
}

static bool
intel_nir_blockify_uniform_loads_intr(nir_builder *b,
                                      nir_intrinsic_instr *intrin,
                                      void *cb_data)
{
   const struct intel_device_info *devinfo = cb_data;

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_ssbo:
      /* BDW PRMs, Volume 7: 3D-Media-GPGPU: OWord Block ReadWrite:
       *
       *    "The surface base address must be OWord-aligned."
       *
       * We can't make that guarantee with SSBOs where the alignment is
       * 4bytes.
       */
      if (devinfo->ver < 9)
         return false;

      if (nir_src_is_divergent(&intrin->src[1]))
         return false;

      if (intrin->def.bit_size != 32)
         return false;

      /* Without the LSC, we can only do block loads of at least 4dwords (1
       * oword).
       */
      if (!devinfo->has_lsc && intrin->def.num_components < 4)
         return false;

      b->cursor = nir_before_instr(&intrin->instr);

      nir_def *new_value =
         intrin->intrinsic == nir_intrinsic_load_ubo ?
         nir_load_ubo_uniform_block_intel(
            b,
            intrin->def.num_components,
            intrin->def.bit_size,
            intrin->src[0].ssa,
            intrin->src[1].ssa,
            .access = nir_intrinsic_access(intrin),
            .align_mul = nir_intrinsic_align_mul(intrin),
            .align_offset = nir_intrinsic_align_offset(intrin),
            .base = 0,
            .range = nir_intrinsic_range(intrin)) :
         nir_load_ssbo_uniform_block_intel(
            b,
            intrin->def.num_components,
            intrin->def.bit_size,
            intrin->src[0].ssa,
            intrin->src[1].ssa,
            .access = nir_intrinsic_access(intrin),
            .align_mul = nir_intrinsic_align_mul(intrin),
            .align_offset = nir_intrinsic_align_offset(intrin),
            .base = 0);
      new_value->loop_invariant = intrin->def.loop_invariant;
      new_value->divergent = false;

      nir_def_replace(&intrin->def, new_value);
      return true;

   case nir_intrinsic_load_shared:
      /* Block loads on shared memory are not supported before Icelake. */
      if (devinfo->ver < 11)
         return false;

      if (nir_src_is_divergent(&intrin->src[0]))
         return false;

      if (intrin->def.bit_size != 32)
         return false;

      /* Without the LSC, we have to use OWord Block Load messages (the one
       * that requires OWord aligned offsets, too).
       */
      if (!devinfo->has_lsc &&
          (intrin->def.num_components < 4 ||
           nir_intrinsic_align(intrin) < 16))
         return false;

      intrin->intrinsic = nir_intrinsic_load_shared_uniform_block_intel;
      return true;

   case nir_intrinsic_load_global_constant:
      if (nir_src_is_divergent(&intrin->src[0]))
         return false;

      if (intrin->def.bit_size != 32)
         return false;

      /* Without the LSC, we can only do block loads of at least 4dwords (1
       * oword).
       */
      if (!devinfo->has_lsc && intrin->def.num_components < 4)
         return false;

      intrin->intrinsic = nir_intrinsic_load_global_constant_uniform_block_intel;
      return true;

   default:
      return false;
   }
}

bool
intel_nir_blockify_uniform_loads(nir_shader *shader,
                                 const struct intel_device_info *devinfo)
{
   nir_divergence_analysis(shader);

   return nir_shader_intrinsics_pass(shader,
                                     intel_nir_blockify_uniform_loads_intr,
                                     nir_metadata_control_flow,
                                     (void *) devinfo);
}
