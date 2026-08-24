/*
 * Copyright © 2026 Arm Ltd.
 *
 * Derived from nir_lower_mem_access_bit_sizes.c which is:
 * Copyright © 2018 Intel Corporation
 * Copyright © 2023 Collabora, Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "pan_nir.h"

/* When accessing scratch memory, there are limitations on the width of
 * LOAD/STORE operations that can be performed in case the address is divergent.
 *
 * This pass lowers restricted width LOAD/STORE operations to multiples of
 * allowed ones. */

static nir_intrinsic_instr *
dup_mem_intrinsic(nir_builder *b, nir_intrinsic_instr *intr,
                  nir_io_offset offset, unsigned align_mul,
                  unsigned align_offset, nir_def *data, unsigned num_components,
                  unsigned bit_size)
{
   const nir_intrinsic_info *info = &nir_intrinsic_infos[intr->intrinsic];

   nir_intrinsic_instr *dup =
      nir_intrinsic_instr_create(b->shader, intr->intrinsic);

   nir_src *intrin_offset_src = nir_get_io_offset_src(intr);
   for (unsigned i = 0; i < info->num_srcs; i++) {
      if (i == 0 && data != NULL) {
         assert(!info->has_dest);
         assert(&intr->src[i] != intrin_offset_src);
         dup->src[i] = nir_src_for_ssa(data);
      } else if (&intr->src[i] == intrin_offset_src) {
         /* Handled by nir_set_io_offset below. */
      } else {
         dup->src[i] = nir_src_for_ssa(intr->src[i].ssa);
      }
   }

   dup->num_components = num_components;
   for (unsigned i = 0; i < info->num_index_slots; i++)
      dup->const_index[i] = intr->const_index[i];

   nir_set_io_offset(dup, offset);
   nir_intrinsic_set_align(dup, align_mul, align_offset);

   if (info->has_dest) {
      nir_def_init(&dup->instr, &dup->def, num_components, bit_size);
      dup->def.divergent = intr->def.divergent;
   } else {
      nir_intrinsic_set_write_mask(dup, (1 << num_components) - 1);
   }

   nir_builder_instr_insert(b, &dup->instr);

   return dup;
}

/* At most we'll need to split a scratch access into four. */
#define MAX_DIVERGENT_SPLIT 4

static bool
lower_divergent_scratch(nir_builder *b, nir_intrinsic_instr *intr,
                        UNUSED void *data)
{
   unsigned bits = 0;
   bool is_load = false;
   switch (intr->intrinsic) {
   case nir_intrinsic_load_scratch:
      if (!nir_src_is_divergent(&intr->src[0]))
         return false;
      is_load = true;
      bits = intr->num_components * intr->def.bit_size;
      break;
   case nir_intrinsic_store_scratch:
      if (!nir_src_is_divergent(&intr->src[1]))
         return false;
      bits = intr->num_components * nir_src_bit_size(intr->src[0]);
      break;
   default:
      return false;
   }

   unsigned num_chunks = 0;
   unsigned bit_sizes[MAX_DIVERGENT_SPLIT];

   switch (bits) {
   case 8:
   case 16:
   case 32:
      /* No lowering required */
      return false;
   case 24:
      if (is_load) {
         bit_sizes[num_chunks++] = 16;
         bit_sizes[num_chunks++] = 8;
      } else {
         /* 24-bit stores do not have alignment restrictions, but 16-bit stores
          * must not straddle 4 bytes boundaries. */
         bit_sizes[num_chunks++] = 8;
         bit_sizes[num_chunks++] = 8;
         bit_sizes[num_chunks++] = 8;
      }
      break;
   case 48:
      bit_sizes[num_chunks++] = 32;
      bit_sizes[num_chunks++] = 16;
      break;
   case 64:
      bit_sizes[num_chunks++] = 32;
      bit_sizes[num_chunks++] = 32;
      break;
   case 96:
      bit_sizes[num_chunks++] = 32;
      bit_sizes[num_chunks++] = 32;
      bit_sizes[num_chunks++] = 32;
      break;
   case 128:
      bit_sizes[num_chunks++] = 32;
      bit_sizes[num_chunks++] = 32;
      bit_sizes[num_chunks++] = 32;
      bit_sizes[num_chunks++] = 32;
      break;
   default:
      UNREACHABLE("Unexpected bit size");
   }
   assert(num_chunks <= MAX_DIVERGENT_SPLIT);

   const unsigned align_mul = nir_intrinsic_align_mul(intr);
   const unsigned whole_align_offset = nir_intrinsic_align_offset(intr);
   b->cursor = nir_after_instr(&intr->instr);
   unsigned byte_offset = 0;

   if (is_load) {
      nir_def *chunks[MAX_DIVERGENT_SPLIT];

      for (unsigned i = 0; i < num_chunks; i++) {
         nir_io_offset chunk_offset = nir_io_offset_iadd(b, intr, byte_offset);
         const unsigned chunk_align_offset =
            (whole_align_offset + byte_offset) % align_mul;

         nir_intrinsic_instr *load =
            dup_mem_intrinsic(b, intr, chunk_offset, align_mul,
                              chunk_align_offset, NULL, 1, bit_sizes[i]);
         chunks[i] = &load->def;
         byte_offset += bit_sizes[i] / 8;
      }

      nir_def *result = nir_extract_bits(
         b, chunks, num_chunks, 0, intr->num_components, intr->def.bit_size);
      result->divergent = intr->def.divergent;
      nir_def_replace(&intr->def, result);

      return true;
   }

   assert(!is_load);
   for (unsigned i = 0; i < num_chunks; i++) {
      nir_def *value = intr->src[0].ssa;
      nir_def *packed =
         nir_extract_bits(b, &value, 1, byte_offset * 8, 1, bit_sizes[i]);

      nir_io_offset chunk_offset = nir_io_offset_iadd(b, intr, byte_offset);
      const unsigned chunk_align_offset =
         (whole_align_offset + byte_offset) % align_mul;

      dup_mem_intrinsic(b, intr, chunk_offset, align_mul, chunk_align_offset,
                        packed, 1, bit_sizes[i]);
      byte_offset += bit_sizes[i] / 8;
   }

   nir_instr_remove(&intr->instr);
   return true;
}

bool
pan_nir_lower_divergent_scratch(nir_shader *shader, unsigned arch)
{
   /* The divergence limitation was added in v11. */
   if (arch < 11)
      return false;

   return nir_shader_intrinsics_pass(shader, lower_divergent_scratch,
                                     nir_metadata_control_flow, NULL);
}
