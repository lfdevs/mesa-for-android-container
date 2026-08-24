/*
 * Copyright 2023 Valve Corpoation
 * Copyright 2020 Raspberry Pi Ltd
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"
#include "nir_builder.h"
#include "nir_intrinsics_indices.h"
#include "util/format/u_format.h"

/*
 * The robustImageAccess2 specification says: "Reads, atomic
 * read-modify-write operations, or fetches from images outside of the
 * checked dimensions will return zero values, with (0,0,1) values
 * inserted for missing G, B, or A components based on the format."
 *
 * This means, an out-of-bounds access to an R, RG, or RGB format will return
 * (0, 0, 0, 1). For an RGBA format, such as VK_FORMAT_R8G8B8A8_UNORM,
 * it will return (0, 0, 0, 0) as no components are missing.
 */
static nir_def *
image_robust2_oob_value(nir_builder *b, nir_intrinsic_instr *instr)
{
   unsigned num_components = instr->def.num_components;
   unsigned bit_size = instr->def.bit_size;
   nir_alu_type dest_type = nir_intrinsic_dest_type(instr);
   enum pipe_format format = nir_intrinsic_format(instr);
   nir_def *zero = nir_imm_zero(b, 1, bit_size);
   nir_def *one;
   if (nir_alu_type_get_base_type(dest_type) == nir_type_float)
      one = nir_imm_floatN_t(b, 1, bit_size);
   else
      one = nir_imm_intN_t(b, 1, bit_size);

   const struct util_format_description *desc = util_format_description(format);
   nir_def *components[NIR_MAX_VEC_COMPONENTS];
   for (unsigned i = 0; i < num_components; i++)
      components[i] = desc->swizzle[i] == PIPE_SWIZZLE_1 ? one : zero;

   return nir_vec(b, components, num_components);
}

static void
rewrite_offset(nir_builder *b, nir_intrinsic_instr *instr,
               uint32_t type_sz, uint32_t offset_src, nir_def *size)
{
   /* Compute the maximum offset being accessed and if it is out of bounds
    * rewrite it to 0 to ensure the access is within bounds.
    */
   const uint32_t access_size = instr->num_components * type_sz;
   nir_def *max_access_offset =
      nir_iadd_imm(b, instr->src[offset_src].ssa, access_size - 1);
   nir_def *offset =
      nir_bcsel(b, nir_uge(b, max_access_offset, size), nir_imm_int(b, 0),
                instr->src[offset_src].ssa);

   /* Rewrite offset */
   nir_src_rewrite(&instr->src[offset_src], offset);
}

/*
 * Wrap a intrinsic in an if, predicated on a "valid" condition. If the
 * intrinsic produces a destination, it will be zero in the invalid case.
 */
static void
wrap_in_if(nir_builder *b, nir_intrinsic_instr *instr, nir_def *valid, bool is_load)
{
   bool has_dest = nir_intrinsic_infos[instr->intrinsic].has_dest;
   nir_def *res, *zero;

   if (has_dest) {
         zero = is_load ? image_robust2_oob_value(b, instr) :
               nir_imm_zero(b, instr->def.num_components, instr->def.bit_size);
   }

   nir_push_if(b, valid);
   {
      nir_instr *orig = nir_instr_clone(b->shader, &instr->instr);
      nir_builder_instr_insert(b, orig);

      if (has_dest)
         res = &nir_instr_as_intrinsic(orig)->def;
   }
   nir_pop_if(b, NULL);

   if (has_dest)
      nir_def_rewrite_uses(&instr->def, nir_if_phi(b, res, zero));

   /* We've cloned and wrapped, so drop original instruction */
   nir_instr_remove(&instr->instr);
}

static void
predicate_offset(nir_builder *b, nir_intrinsic_instr *instr, uint32_t type_sz,
                 uint32_t offset_src, nir_def *size)
{
   /* Only the components selected by the write mask are stored, each at
    * offset + component * type_sz. Bound the access by the highest written
    * component so an in-bounds low component is not dropped just because a
    * higher, unwritten component would be out of bounds.
    */
   unsigned components = instr->num_components;
   if (nir_intrinsic_has_write_mask(instr))
      components = util_last_bit(nir_intrinsic_write_mask(instr));

   const uint32_t access_size = components * type_sz;
   nir_def *max_access_offset =
      nir_iadd_imm(b, instr->src[offset_src].ssa, access_size - 1);
   nir_def *valid = nir_ult(b, max_access_offset, size);

   wrap_in_if(b, instr, valid, false);
}

static void
lower_buffer_load(nir_builder *b, nir_intrinsic_instr *instr)
{
   uint32_t type_sz = instr->def.bit_size / 8;
   nir_def *size;
   nir_def *index = instr->src[0].ssa;

   if (instr->intrinsic == nir_intrinsic_load_ubo) {
      size = nir_get_ubo_size(b, 32, index);
   } else {
      size = nir_get_ssbo_size(b, 32, index);
   }

   rewrite_offset(b, instr, type_sz, 1, size);
}

static void
lower_buffer_store(nir_builder *b, nir_intrinsic_instr *instr)
{
   uint32_t type_sz = nir_src_bit_size(instr->src[0]) / 8;
   predicate_offset(b, instr, type_sz, 2,
                    nir_get_ssbo_size(b, 32, instr->src[1].ssa));
}

static void
lower_buffer_atomic(nir_builder *b, nir_intrinsic_instr *instr)
{
   predicate_offset(b, instr, 4, 1, nir_get_ssbo_size(b, 32, instr->src[0].ssa));
}

static void
lower_buffer_shared(nir_builder *b, nir_intrinsic_instr *instr)
{
   uint32_t type_sz, offset_src;
   if (instr->intrinsic == nir_intrinsic_load_shared) {
      offset_src = 0;
      type_sz = instr->def.bit_size / 8;
   } else if (instr->intrinsic == nir_intrinsic_store_shared) {
      offset_src = 1;
      type_sz = nir_src_bit_size(instr->src[0]) / 8;
   } else {
      /* atomic */
      offset_src = 0;
      type_sz = 4;
   }

   rewrite_offset(b, instr, type_sz, offset_src,
                  nir_imm_int(b, b->shader->info.shared_size));
}

static nir_image_intrinsic_type
image_intrinsic_type(nir_intrinsic_op intrinsic)
{
   switch (intrinsic) {
   case nir_intrinsic_image_load:
   case nir_intrinsic_image_store:
   case nir_intrinsic_image_atomic:
   case nir_intrinsic_image_atomic_swap:
      return nir_image_intrinsic_type_default;

   case nir_intrinsic_bindless_image_load:
   case nir_intrinsic_bindless_image_store:
   case nir_intrinsic_bindless_image_atomic:
   case nir_intrinsic_bindless_image_atomic_swap:
      return nir_image_intrinsic_type_bindless;

   case nir_intrinsic_image_deref_load:
   case nir_intrinsic_image_deref_store:
   case nir_intrinsic_image_deref_atomic:
   case nir_intrinsic_image_deref_atomic_swap:
      return nir_image_intrinsic_type_deref;

   case nir_intrinsic_image_heap_load:
   case nir_intrinsic_image_heap_store:
   case nir_intrinsic_image_heap_atomic:
   case nir_intrinsic_image_heap_atomic_swap:
      return nir_image_intrinsic_type_heap;

   default:
      UNREACHABLE("Invalid NIR image intrinsic");
   }
}

static void
lower_image(nir_builder *b, nir_intrinsic_instr *instr,
            nir_image_intrinsic_type type, bool is_load)
{
   enum glsl_sampler_dim dim = nir_intrinsic_image_dim(instr);
   uint32_t num_coords = nir_image_intrinsic_coord_components(instr);
   bool is_array = nir_intrinsic_image_array(instr);
   nir_def *coord = instr->src[1].ssa;

   /* Get image size. imageSize for cubes returns the size of a single face. */
   unsigned size_components = num_coords;
   if (dim == GLSL_SAMPLER_DIM_CUBE && !is_array)
      size_components -= 1;

   nir_def *size = nir_image_size(b, size_components, 32,
                                  instr->src[0].ssa, nir_imm_int(b, 0),
                                  .image_array = is_array, .image_dim = dim);

   switch (type) {
   case nir_image_intrinsic_type_default:
      nir_def_as_intrinsic(size)->intrinsic = nir_intrinsic_image_size;
      break;
   case nir_image_intrinsic_type_deref:
      nir_def_as_intrinsic(size)->intrinsic = nir_intrinsic_image_deref_size;
      break;
   case nir_image_intrinsic_type_heap:
      nir_def_as_intrinsic(size)->intrinsic = nir_intrinsic_image_heap_size;
      break;
   default:
      UNREACHABLE("Invalid NIR image intrinsic type");
   }

   if (dim == GLSL_SAMPLER_DIM_CUBE) {
      nir_def *z = is_array ? nir_imul_imm(b, nir_channel(b, size, 2), 6)
                            : nir_imm_int(b, 6);

      size = nir_vec3(b, nir_channel(b, size, 0), nir_channel(b, size, 1), z);
   }

   nir_def *in_bounds = nir_ball(b, nir_ult(b, coord, size));

   if (dim == GLSL_SAMPLER_DIM_MS) {
      nir_def *sample = instr->src[2].ssa;
      nir_def *samples = nir_image_samples(b, 32, instr->src[0].ssa,
                                           .image_array = is_array, .image_dim = dim);

      switch (type) {
      case nir_image_intrinsic_type_default:
         nir_def_as_intrinsic(samples)->intrinsic = nir_intrinsic_image_samples;
         break;
      case nir_image_intrinsic_type_deref:
         nir_def_as_intrinsic(samples)->intrinsic = nir_intrinsic_image_deref_samples;
         break;
      case nir_image_intrinsic_type_heap:
         nir_def_as_intrinsic(samples)->intrinsic = nir_intrinsic_image_heap_samples;
         break;
      default:
         UNREACHABLE("Invalid NIR image intrinsic type");
      }

      in_bounds = nir_iand(b, in_bounds, nir_ult(b, sample, samples));
   }

   /* Only execute if coordinates are in-bounds. Otherwise, return zero. */
   wrap_in_if(b, instr, in_bounds, is_load);
}

struct pass_opts {
   nir_intrin_filter_cb filter;
   const void *data;
};

static bool
lower(nir_builder *b, nir_intrinsic_instr *intr, void *_opts)
{
   const struct pass_opts *opts = _opts;
   if (!opts->filter(intr, opts->data))
      return false;

   b->cursor = nir_before_instr(&intr->instr);

   switch (intr->intrinsic) {
   case nir_intrinsic_image_load:
   case nir_intrinsic_image_deref_load:
   case nir_intrinsic_image_heap_load:
      lower_image(b, intr, image_intrinsic_type(intr->intrinsic), true);
      return true;
   case nir_intrinsic_image_store:
   case nir_intrinsic_image_atomic:
   case nir_intrinsic_image_atomic_swap:
   case nir_intrinsic_image_deref_store:
   case nir_intrinsic_image_deref_atomic:
   case nir_intrinsic_image_deref_atomic_swap:
   case nir_intrinsic_image_heap_store:
   case nir_intrinsic_image_heap_atomic:
   case nir_intrinsic_image_heap_atomic_swap:
      lower_image(b, intr, image_intrinsic_type(intr->intrinsic), false);
      return true;

   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_ssbo:
      lower_buffer_load(b, intr);
      return true;
   case nir_intrinsic_store_ssbo:
      lower_buffer_store(b, intr);
      return true;
   case nir_intrinsic_ssbo_atomic:
   case nir_intrinsic_ssbo_atomic_swap:
      lower_buffer_atomic(b, intr);
      return true;

   case nir_intrinsic_store_shared:
   case nir_intrinsic_load_shared:
   case nir_intrinsic_shared_atomic:
   case nir_intrinsic_shared_atomic_swap:
      /* Vulkan's robustBufferAccess feature is only concerned with buffers that
       * are bound through descriptor sets, so shared memory is not included,
       * but this lowering may be useful for debugging.
       */
      lower_buffer_shared(b, intr);
      return true;

   default:
      UNREACHABLE("driver requested lowering for unsupported intrinsic");
   }
}

/*
 * Buffer/image robustness lowering with robustBufferAccess/robustImageAccess
 * semantics. This is sufficient for GL, but not for D3D. However, Vulkan
 * drivers get buffer robustness lowered via nir_lower_explicit_io.
 */
bool
nir_lower_robust_access(nir_shader *s, nir_intrin_filter_cb filter,
                        const void *data)
{
   struct pass_opts opt = { .filter = filter, .data = data };
   return nir_shader_intrinsics_pass(s, lower, nir_metadata_none, &opt);
}
