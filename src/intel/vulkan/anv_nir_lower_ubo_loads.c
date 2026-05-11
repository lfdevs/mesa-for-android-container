/*
 * Copyright © 2020 Intel Corporation
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

#include "anv_nir.h"
#include "nir_builder.h"

static nir_def *
lower_ubo_load_addr(nir_builder *b, nir_def *base_addr,
                    nir_def *offset, nir_def *bound,
                    unsigned load_size)
{
   nir_def *addr = nir_iadd(b, base_addr, nir_u2u64(b, offset));

   if (bound) {
      addr =
         nir_bcsel(b,
                   nir_ult(b, nir_iadd_imm(b, offset, load_size - 1), bound),
                   addr,
                   nir_pack_64_2x32_split(
                      b,
                      nir_load_reloc_const_intel(b, BRW_SHADER_RELOC_NULL_CACHELINE_ADDR_LOW),
                      nir_load_reloc_const_intel(b, BRW_SHADER_RELOC_NULL_CACHELINE_ADDR_HIGH)));
   }

   return addr;
}

static bool
lower_ubo_load_instr(nir_builder *b, nir_intrinsic_instr *load,
                     UNUSED void *_data)
{
   if (load->intrinsic != nir_intrinsic_load_global_constant_offset &&
       load->intrinsic != nir_intrinsic_load_global_constant_bounded)
      return false;

   b->cursor = nir_before_instr(&load->instr);

   nir_def *base_addr = load->src[0].ssa;
   nir_def *bound = NULL;
   if (load->intrinsic == nir_intrinsic_load_global_constant_bounded)
      bound = load->src[2].ssa;

   unsigned bit_size = load->def.bit_size;
   assert(bit_size >= 8 && bit_size % 8 == 0);
   unsigned byte_size = bit_size / 8;

   nir_def *val;
   if (!nir_src_is_divergent(&load->src[0]) && nir_src_is_const(load->src[1])) {
      uint32_t offset = nir_src_as_uint(load->src[1]);

      /* Things should be component-aligned. */
      assert(offset % byte_size == 0);

      assert(ANV_UBO_ALIGNMENT == 64);

      unsigned suboffset = offset % 64;
      unsigned aligned_offset = offset - suboffset;

      /* Load two just in case we go over a 64B boundary */
      nir_def *data[2];
      for (unsigned i = 0; i < 2; i++) {
         nir_def *addr =
            lower_ubo_load_addr(b, base_addr,
                                nir_imm_int(b, aligned_offset + i * 64),
                                bound, 1);

         data[i] = nir_load_global_constant_uniform_block_intel(
            b, 16, 32, addr,
            .access = nir_intrinsic_access(load),
            .align_mul = 64);
      }

      if (bound) {
         nir_def* offsets =
            nir_imm_uvec8(b, aligned_offset, aligned_offset + 16,
                          aligned_offset + 32, aligned_offset + 48,
                          aligned_offset + 64, aligned_offset + 80,
                          aligned_offset + 96, aligned_offset + 112);
         nir_def* mask =
            nir_bcsel(b, nir_ult(b, offsets, bound),
                      nir_imm_int(b, 0xFFFFFFFF),
                      nir_imm_int(b, 0x00000000));

         for (unsigned i = 0; i < 2; i++) {
            /* We prepared a mask where every 1 bit of mask covers 4 bits of the
             * UBO block we've loaded, when we apply it we'll sign extend each
             * byte of the mask to a dword to get the final bitfield, this can
             * be optimized because Intel HW allows instructions to mix several
             * types and perform the sign extensions implicitly.
             */
            data[i] =
               nir_iand(b,
                        nir_i2iN(b, nir_extract_bits(b, &mask, 1, i * 128, 16, 8), 32),
                        data[i]);
         }
      }

      val = nir_extract_bits(b, data, 2, suboffset * 8,
                             load->num_components, bit_size);
   } else {
      nir_def *offset = load->src[1].ssa;
      unsigned load_size = byte_size * load->num_components;

      nir_def *addr = lower_ubo_load_addr(b, base_addr, offset, bound, load_size);
      val = nir_load_global_constant(b, load->def.num_components,
                                     load->def.bit_size, addr,
                                     .access = nir_intrinsic_access(load),
                                     .align_mul = nir_intrinsic_align_mul(load),
                                     .align_offset = nir_intrinsic_align_offset(load));
   }

   nir_def_replace(&load->def, val);

   return true;
}

bool
anv_nir_lower_ubo_loads(nir_shader *shader)
{
   return nir_shader_intrinsics_pass(shader, lower_ubo_load_instr,
                                     nir_metadata_none, NULL);
}
