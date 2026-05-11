/*
 * Copyright © 2023 Valve Corporation
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
 *
 */

#include "ac_nir.h"
#include "nir_builder.h"

static void
replace_with_formatted_load_buffer_amd(nir_builder *b, nir_def *old_def, nir_deref_instr *deref,
                                       nir_def *handle, nir_def *heap_offset, nir_def *vindex,
                                       unsigned access, nir_variable_mode mode,
                                       unsigned backend_flags, nir_alu_type dest_type)
{
   nir_def *zero = nir_imm_int(b, 0);

   nir_def *desc = nir_build_tex(b, nir_texop_descriptor_amd,
                                 .texture_deref = deref,
                                 .texture_handle = handle,
                                 .texture_heap_offset = heap_offset,
                                 .dim = GLSL_SAMPLER_DIM_BUF,
                                 .dest_type = nir_type_uint32,
                                 .can_speculate = access & ACCESS_CAN_SPECULATE,
                                 .backend_flags = backend_flags);
   desc->num_components = 4; /* buffer descriptors have 4 components */

   assert(vindex->num_components == 1);

   /* Buffers don't support 16-bit vindex. */
   if (vindex->bit_size == 16)
      vindex = nir_u2u32(b, vindex);

   /* The descriptor load isn't always speculatable, but the buffer load is, or we can treat it as
    * always speculatable in nir_instr_can_speculate instead of setting this flag.
    */
   access |= ACCESS_CAN_SPECULATE;

   bool is_sparse = access & ACCESS_SPARSE;
   unsigned dest_comp_mask = nir_def_components_read(old_def);
   unsigned sparse_comp_bit = is_sparse ? BITFIELD_BIT(old_def->num_components - 1) : 0;
   unsigned data_comp_mask = dest_comp_mask & ~sparse_comp_bit;
   bool sparse_used = dest_comp_mask & sparse_comp_bit;
   /* buffer_load_format must return at least 1 component. */
   unsigned num_data_components = MAX2(util_last_bit(data_comp_mask), 1);
   unsigned num_total_components, bit_size;

   access |= ACCESS_USES_FORMAT_AMD;
   if (is_sparse && !sparse_used)
      access &= ~ACCESS_SPARSE;

   /* I don't think ACO can do D16 (16-bit result) with TFE (always 32-bit) yet.
    * ac_nir_to_llvm can.
    */
   assert(!is_sparse || old_def->bit_size != 16);

   /* Get the 32-bit representation of the 64-bit def type. The 64-bit sparse flag is removed and
    * replaced by a 32-bit sparse flag.
    *
    * Only R64_UINT and R64_SINT is supported. X is in XY of the result, W in ZW. (TODO: is W always 1?)
    * So we need 2 32-bit data components if X is needed, and 4 32-bit data components if W is needed.
    */
   if (old_def->bit_size == 64) {
      num_total_components = (num_data_components < 4 ? 2 : 4) + (is_sparse && sparse_used);
      bit_size = 32;

      assert(dest_type & 64);
      assert(dest_type & (nir_type_int | nir_type_uint));
      dest_type &= ~64;
      dest_type |= 32;
   } else {
      /* This eliminates unused components between the result data and the sparse flag. */
      num_total_components = num_data_components + (is_sparse && sparse_used);
      bit_size = old_def->bit_size;
   }

   nir_def *result = nir_load_buffer_amd(b, num_total_components, bit_size, desc, zero, zero,
                                         vindex,
                                         .memory_modes = mode,
                                         .access = access,
                                         .dest_type = dest_type);

   if (old_def->bit_size == 64) {
      nir_def **vec = alloca(sizeof(nir_def*) * old_def->num_components);
      nir_def *undef64 = nir_undef(b, 1, 64);

      /* The 64-bit result is: (xy, 0, 0, zw, sparse). */
      if (0 < old_def->num_components - is_sparse) {
         if (2 <= result->num_components - is_sparse)
            vec[0] = nir_pack_64_2x32(b, nir_channels(b, result, BITFIELD_MASK(2)));
         else
            vec[0] = undef64;
      }
      if (1 < old_def->num_components - is_sparse)
         vec[1] = nir_imm_int64(b, 0);
      if (2 < old_def->num_components - is_sparse)
         vec[2] = vec[1];
      if (3 < old_def->num_components - is_sparse) {
         if (4 <= result->num_components)
            vec[3] = nir_pack_64_2x32(b, nir_channels(b, result, BITFIELD_RANGE(2, 2)));
         else
            vec[3] = undef64;
      }
      if (is_sparse) {
         if (sparse_used)
            vec[old_def->num_components - 1] = nir_u2u64(b, nir_channel(b, result, num_total_components - 1));
         else
            vec[old_def->num_components - 1] = undef64;
      }

      result = nir_vec(b, vec, old_def->num_components);
   } else if (num_total_components != old_def->num_components) {
      assert(num_total_components < old_def->num_components);

      /* We removed unused components between the last used data component and the sparse flag.
       * Add the unused components back as undef.
       */
      nir_def **vec = alloca(sizeof(nir_def*) * old_def->num_components);
      nir_def *undef = nir_undef(b, 1, old_def->bit_size);
      unsigned i = 0;

      for (; i < num_data_components; i++)
         vec[i] = nir_channel(b, result, i);

      for (; i < old_def->num_components - is_sparse; i++)
         vec[i] = undef;

      if (is_sparse) {
         if (sparse_used)
            vec[old_def->num_components - 1] = nir_channel(b, result, num_total_components - 1);
         else
            vec[old_def->num_components - 1] = undef;
      }

      result = nir_vec(b, vec, old_def->num_components);
   }

   nir_def_replace(old_def, result);
}

static bool
lower_tex(nir_builder *b, nir_tex_instr *tex, const ac_nir_lower_image_tex_options *options)
{
   b->cursor = nir_before_instr(&tex->instr);

   if (tex->sampler_dim == GLSL_SAMPLER_DIM_BUF && tex->op == nir_texop_txf) {
      nir_deref_instr *deref = NULL;
      nir_def *handle = NULL;
      nir_def *vindex = NULL;
      nir_def *heap_offset = NULL;

      for (unsigned i = 0; i < tex->num_srcs; i++) {
         switch (tex->src[i].src_type) {
         case nir_tex_src_texture_deref:
            deref = nir_instr_as_deref(nir_def_instr(tex->src[i].src.ssa));
            break;
         case nir_tex_src_texture_handle:
            handle = tex->src[i].src.ssa;
            break;
         case nir_tex_src_coord:
            vindex = tex->src[i].src.ssa;
            break;
         case nir_tex_src_offset:
         case nir_tex_src_texture_offset:
            UNREACHABLE("unexpected tex src for buffer loads");
         case nir_tex_src_texture_heap_offset:
            heap_offset = tex->src[i].src.ssa;
            break;
         default:
            break;
         }
      }

      replace_with_formatted_load_buffer_amd(b, &tex->def, deref, handle, heap_offset, vindex,
                                             ACCESS_RESTRICT | ACCESS_NON_WRITEABLE |
                                             ACCESS_CAN_REORDER |
                                             (tex->can_speculate ? ACCESS_CAN_SPECULATE : 0) |
                                             (tex->texture_non_uniform ? ACCESS_NON_UNIFORM : 0) |
                                             (tex->skip_helpers ? ACCESS_SKIP_HELPERS : 0) |
                                             (tex->is_sparse ? ACCESS_SPARSE : 0),
                                             nir_var_uniform /* nir_var_texture? */, 0,
                                             tex->dest_type);
      return true;
   }

   return false;
}

static bool
lower_image(nir_builder *b, nir_intrinsic_instr *intr, const ac_nir_lower_image_tex_options *options)
{
   /* unexpected intrinsics */
   assert(intr->intrinsic != nir_intrinsic_image_load &&
          intr->intrinsic != nir_intrinsic_image_sparse_load);

   if (nir_intrinsic_has_image_dim(intr) &&
       nir_intrinsic_image_dim(intr) == GLSL_SAMPLER_DIM_BUF &&
       (intr->intrinsic == nir_intrinsic_image_deref_load ||
        intr->intrinsic == nir_intrinsic_image_deref_sparse_load ||
        intr->intrinsic == nir_intrinsic_bindless_image_load ||
        intr->intrinsic == nir_intrinsic_bindless_image_sparse_load ||
        intr->intrinsic == nir_intrinsic_image_heap_load ||
        intr->intrinsic == nir_intrinsic_image_heap_sparse_load)) {
      b->cursor = nir_before_instr(&intr->instr);

      nir_deref_instr *deref = NULL;
      nir_def *handle = NULL;
      nir_def *heap_offset = NULL;

      if (intr->intrinsic == nir_intrinsic_image_deref_load ||
          intr->intrinsic == nir_intrinsic_image_deref_sparse_load)
         deref = nir_instr_as_deref(nir_def_instr(intr->src[0].ssa));
      else if (intr->intrinsic == nir_intrinsic_image_heap_load ||
               intr->intrinsic == nir_intrinsic_image_heap_sparse_load)
         heap_offset = intr->src[0].ssa;
      else
         handle = intr->src[0].ssa;

      bool is_sparse = intr->intrinsic == nir_intrinsic_image_deref_sparse_load ||
                       intr->intrinsic == nir_intrinsic_bindless_image_sparse_load ||
                       intr->intrinsic == nir_intrinsic_image_heap_sparse_load;
      unsigned access = nir_intrinsic_access(intr) | (is_sparse ? ACCESS_SPARSE : 0);
      nir_def *vindex = nir_channel(b, intr->src[1].ssa, 0);

      replace_with_formatted_load_buffer_amd(b, &intr->def, deref, handle, heap_offset, vindex, access,
                                             nir_var_image, AC_NIR_TEX_BACKEND_FLAG_IS_IMAGE,
                                             nir_intrinsic_dest_type(intr));
      return true;
   }

   return false;
}

static bool
lower_image_tex(nir_builder *b, nir_instr *instr, void *options_)
{
   const ac_nir_lower_image_tex_options *options = options_;

   if (instr->type == nir_instr_type_tex)
      return lower_tex(b, nir_instr_as_tex(instr), options);

   if (instr->type == nir_instr_type_intrinsic)
      return lower_image(b, nir_instr_as_intrinsic(instr), options);

   return false;
}

bool
ac_nir_lower_image_tex(nir_shader *nir, const ac_nir_lower_image_tex_options *options)
{
   bool progress = false;

   progress |= nir_shader_instructions_pass(
      nir, lower_image_tex, nir_metadata_control_flow, (void *)options);

   return progress;
}
