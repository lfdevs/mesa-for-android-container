/*
 * Copyright © 2023 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "nak.h"
#include "nak_private.h"
#include "nil.h"

#include "nir_format_convert.h"

static nir_def *
build_load_su_info(nir_builder *b, nir_deref_instr *deref, uint32_t offset)
{
   return nir_image_deref_load_info_nv(b, 1, &deref->def, .base = offset);
}

#define load_su_info(b, d, field) \
   build_load_su_info((b), (d), offsetof(struct nil_su_info, field))

static enum pipe_format
format_for_bits(unsigned bits)
{
   switch (bits) {
   case 8:   return PIPE_FORMAT_R8_UINT;
   case 16:  return PIPE_FORMAT_R16_UINT;
   case 32:  return PIPE_FORMAT_R32_UINT;
   case 64:  return PIPE_FORMAT_R32G32_UINT;
   case 128: return PIPE_FORMAT_R32G32B32A32_UINT;
   default: UNREACHABLE("Unknown number of image format bits");
   }
}

static unsigned
sampler_dim_len(enum glsl_sampler_dim dim)
{
   switch (dim) {
   case GLSL_SAMPLER_DIM_1D:
   case GLSL_SAMPLER_DIM_BUF:
      return 1;
   case GLSL_SAMPLER_DIM_CUBE:
   case GLSL_SAMPLER_DIM_2D:
   case GLSL_SAMPLER_DIM_RECT:
   case GLSL_SAMPLER_DIM_MS:
      return 2;
   case GLSL_SAMPLER_DIM_3D:
      return 3;
   default:
      UNREACHABLE("Unhandled sampler dim");
      return 1;// Never reached
   }
}

static nir_def *
lower_formatted_image_load(nir_builder *b,
                           nir_intrinsic_instr *intrin,
                           enum pipe_format format)
{
   if (format == PIPE_FORMAT_NONE)
      return NULL;

   unsigned bits = util_format_get_blocksizebits(format);

   assert(intrin->intrinsic == nir_intrinsic_suldga_nv);

   nir_intrinsic_set_format(intrin, format_for_bits(bits));

   const unsigned num_raw_components = DIV_ROUND_UP(bits, 32);
   intrin->num_components = num_raw_components;
   intrin->def.num_components = num_raw_components;

   b->cursor = nir_after_instr(&intrin->instr);
   nir_def *rgba = NULL;
   switch (format) {
   case PIPE_FORMAT_R64_UINT:
   case PIPE_FORMAT_R64_SINT:
      rgba = nir_vec4(b, nir_pack_64_2x32(b, &intrin->def),
                         nir_imm_int64(b, 0),
                         nir_imm_int64(b, 0),
                         nir_imm_int64(b, 1));
      break;
   default:
      rgba = nir_format_unpack_rgba(b, &intrin->def, format);
      break;
   }

   return rgba;
}

static nir_def *
load_su_info_clamp(nir_builder *b, nir_deref_instr *deref,
                   unsigned xyz)
{
   /* Array length is always stored in clamp_z */
   enum glsl_sampler_dim dim = glsl_get_sampler_dim(deref->type);
   if (dim == GLSL_SAMPLER_DIM_1D && xyz == 1)
      return load_su_info(b, deref, clamp_z);

   switch (xyz) {
   case 0: return load_su_info(b, deref, clamp_x);
   case 1: return load_su_info(b, deref, clamp_y);
   case 2: return load_su_info(b, deref, clamp_z);
   default: UNREACHABLE("Invalid image dimension");
   }
}

static nir_def *
clamp_coord(nir_builder *b, nir_deref_instr *deref,
            nir_def* coord, int xyz)
{
   nir_def *clamp = load_su_info_clamp(b, deref, xyz);

   const struct nak_nir_suclamp_flags flags = {
      .mode = NAK_SUCLAMP_MODE_STORED_DESCRIPTOR,
      .round = NAK_SUCLAMP_ROUND_R1,
      .is_s32 = false,
      .is_2d = true,
   };

   nir_def *dst = nir_suclamp_nv(b, coord, clamp,
                                 .flags = NAK_AS_U32(flags));
   return nir_channel(b, dst, 0);
}

static void
load_sample_size(nir_builder *b, nir_deref_instr *deref,
                 nir_def **sample_width_log2,
                 nir_def **sample_height_log2)
{
   /* MS width and height are stored in the lower 8 bits of pitch */
   nir_def *pitch = load_su_info(b, deref, pitch);
   *sample_width_log2 = nir_ubitfield_extract_imm(b, pitch, 24, 4);
   *sample_height_log2 = nir_ubitfield_extract_imm(b, pitch, 28, 4);
}

// Kepler only supports suldga, sustga, so we need to compute
// the raw address manually, this is done through a weird dance
// and custom ops.
// - each coordinate is clamped through suclamp
// - compute block offset using gob coordinates (y * pitch + x)
// - merge bitfields (output of suclamps)
//   this will both merge the block coordinates, combine
//   pixel coordinates (in GoB-space) and OR together the OOB
//   predicate
// - compute the effective upper address by combining block_offset,
//   bitfield and base_address
// - combine effective upper address and the combined bitfield that
//   contains the lower 8 bits of global address
// - pass the combined values into suldga/sustga
//
// Linear Layout support:
// Shader cannot know at compile time if an image is in linear format
// or in block format, thus it needs to support both modes using the
// same opcodes, the only difference should be in descriptors.
// The address for linear layout is computed with:
// eff_addr = base_addr + (y * pitch + x)*el_size_B
// Here we first compute off = (y * pitch + x) with imadsp
// then we split what goes in the first 8 bits using subfm
// (in 0..8 it loads off << el_size_B.log2, in 16..32 it
// loads off >> el_size_B.log2)
// For this we need to emit eau = sueau bf.x, bf.y, off
// this would totally screw up block-linear calcs, but we can use
// is_3d=false to skip the third argument only in block-linear.
static void
compute_image_address(nir_builder *b,
                      nir_intrinsic_instr *intrin,
                      enum glsl_sampler_dim sampler_dim,
                      nir_def **addr,
                      nir_def **is_oob)
{
   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);

   const unsigned dim = sampler_dim_len(sampler_dim);
   const bool is_cube = sampler_dim == GLSL_SAMPLER_DIM_CUBE;

   // A CubeMap is a 6-array of 2D images, a CubeMap array is just
   // multiple CubeMap concatenated together, so the cube access is alway
   // treated as an array
   const bool is_array = nir_intrinsic_image_array(intrin) || is_cube;

   // Prevent read/write for null-descriptors
   nir_def *base_addr = load_su_info(b, deref, addr_shifted8);
   *is_oob = nir_ieq_imm(b, base_addr, 0);

   nir_def *coords[3];
   for (int i = 0; i < 3; i++)
      coords[i] = nir_channel(b, intrin->src[1].ssa, i);

   // Lower multi-sample coords
   if (sampler_dim == GLSL_SAMPLER_DIM_MS) {
      nir_def *s = intrin->src[2].ssa;
      nir_def *sw_log2, *sh_log2;
      load_sample_size(b, deref, &sw_log2, &sh_log2);
      nir_def *s_map = load_su_info(b, deref, extra);// multi-sample table
      nir_def *num_samples = nir_ishl(b, nir_imm_int(b, 1),
                                      nir_iadd(b, sw_log2, sh_log2));

      nir_def *s_xy = nir_ushr(b, s_map, nir_imul_imm(b, s, 4));
      nir_def *sx = nir_ubitfield_extract_imm(b, s_xy, 0, 2);
      nir_def *sy = nir_ubitfield_extract_imm(b, s_xy, 2, 2);

      nir_def *sw = nir_ishl(b, nir_imm_int(b, 1), sw_log2);
      nir_def *sh = nir_ishl(b, nir_imm_int(b, 1), sh_log2);

      nir_def *x = nir_imad(b, coords[0], sw, sx);
      nir_def *y = nir_imad(b, coords[1], sh, sy);

      // Check if OOB
      *is_oob = nir_ior(b, *is_oob, nir_uge(b, s, num_samples));

      coords[0] = x;
      coords[1] = y;
   }

   // Clamp coordinates
   // This computes a bitfield with the following info inside:
   // - Block coordinates
   // - GoB coordinates
   // - predicate for OOB access
   // - Wether the clamp is using pitch_linear
   // - n. of block tiles for the coordinate
   nir_def *clamped_x = clamp_coord(b, deref, coords[0], 0);
   nir_def *clamped_y = dim >= 2 ? clamp_coord(b, deref, coords[1], 1) : nir_imm_int(b, 0);
   nir_def *clamped_z = dim >= 3 ? clamp_coord(b, deref, coords[2], 2) : nir_imm_int(b, 0);

   // For arrays the clamp is "plain", no bitfield is computed
   // just the OOB predicate.
   nir_def *array_idx = NULL;
   if (is_array) {
      nir_def *clamp = load_su_info_clamp(b, deref, dim);
      nir_def *coord = coords[dim];

      const struct nak_nir_suclamp_flags flags = {
         .mode = NAK_SUCLAMP_MODE_PITCH_LINEAR,
         .round = NAK_SUCLAMP_ROUND_R1,
         .is_s32 = false,
         .is_2d = false,
      };

      nir_def *dst = nir_suclamp_nv(b, coord, clamp,
                                    .flags = NAK_AS_U32(flags));
      array_idx = nir_channel(b, dst, 0);
      *is_oob = nir_ior(b, *is_oob, nir_ine_imm(b, nir_channel(b, dst, 1), 0));
   }

   // Compute offset
   // for Block-Linear: GOB coordinates (offset that contributes *64)
   // for Pitch-Linear: offset in pixels (y * pitch) + x
   // for Buffer: offset = x
   nir_def *off;
   if (dim == 1) {
      // This can be only 16 bits because it's only tile coordinates
      // (actually it's 20 bits in pitch-linear mode, we don't support
      // images that big)
      off = nir_iand_imm(b, clamped_x, 0xffff);
   } else if (dim == 2) {
      // off = clamped.y * pitch + clamped.x
      nir_def *pitch = load_su_info(b, deref, pitch);

      const struct nak_nir_imadsp_flags flags = {
         .src0 = NAK_IMAD_TYPE_U16_LO,
         .src1 = NAK_IMAD_TYPE_U24,
         .src2 = NAK_IMAD_TYPE_U16_LO,
         .params_from_src1 = false,
      };
      off = nir_imadsp_nv(b, clamped_y, pitch, clamped_x,
                          .flags = NAK_AS_U32(flags));
   } else {
      assert(dim == 3);
      // off = (clamped.z * height + clamped.y) * pitch + clamped.x
      // height is the height in blocks, we can compute this by doing a
      // block linear clamp with the maximum value.
      // (block shift-right is applied by suclamp)
      nir_def *dim_y = load_su_info_clamp(b, deref, 1);
      const struct nak_nir_suclamp_flags clamp_flags = {
         .mode = NAK_SUCLAMP_MODE_BLOCK_LINEAR,
         .round = NAK_SUCLAMP_ROUND_R1,
         .is_s32 = false,
         .is_2d = false,
      };
      nir_def *max_y = nir_suclamp_nv(b, nir_imm_int(b, -1), dim_y,
                                      .flags=NAK_AS_U32(clamp_flags));

      // max_y is still a bitfield, we can add 1 but we must only use the
      // lower 16 bits of height_b.
      nir_def *height_b = nir_iadd_imm(b, nir_channel(b, max_y, 0), 1);

      const struct nak_nir_imadsp_flags flags_zy = {
         .src0 = NAK_IMAD_TYPE_U16_LO,
         .src1 = NAK_IMAD_TYPE_U16_LO,
         .src2 = NAK_IMAD_TYPE_U16_LO,
         .params_from_src1 = false,
      };
      nir_def *off_2d = nir_imadsp_nv(b, clamped_z, height_b, clamped_y,
                                      .flags = NAK_AS_U32(flags_zy));

      nir_def *pitch = load_su_info(b, deref, pitch);
      const struct nak_nir_imadsp_flags flags = {
         .src0 = NAK_IMAD_TYPE_U32,
         .src1 = NAK_IMAD_TYPE_U24,
         .src2 = NAK_IMAD_TYPE_U16_LO,
         .params_from_src1 = false,
      };
      off = nir_imadsp_nv(b, off_2d, pitch, clamped_x,
                          .flags = NAK_AS_U32(flags));
   }

   // Compute merged bitfield
   nir_def *bf;
   {
      // bf, pred = subfm clamped.x, clamped.y, clamped.z
      nir_def *bfz;
      bool is_3d = dim >= 3;

      if (dim == 2 && !is_array) {
         // Special case for pitch_linear support, see comment above.
         bfz = off;
      } else {
         bfz = clamped_z;
      }

      nir_def *combined = nir_subfm_nv(b, clamped_x, clamped_y, bfz,
                                       .flags = is_3d);
      bf = nir_channel(b, combined, 0);
      nir_def *bfm_oob = nir_ine_imm(b, nir_channel(b, combined, 1), 0);
      *is_oob = nir_ior(b, *is_oob, bfm_oob);
   }

   nir_def *eau = nir_sueau_nv(b, off, bf, base_addr);

   // Apply array layer offset
   if (is_array) {
      nir_def *array_stride = load_su_info(b, deref, array_stride_shifted8);

      // Note: this only works because array_idx has been plain-clamped
      // so it's not a bitfield (and we can read more than u16)
      const struct nak_nir_imadsp_flags flags = {
         .src0 = NAK_IMAD_TYPE_U32,
         .src1 = NAK_IMAD_TYPE_U24,
         .src2 = NAK_IMAD_TYPE_U32,
         .params_from_src1 = false,
      };

      eau = nir_imadsp_nv(b, array_idx, array_stride, eau,
                          .flags = NAK_AS_U32(flags));
   }

   *addr = nir_vec2(b, bf, eau);
}

// Buffer address calculation is much simpler than images
// In the end we need the global address to reach in two registers
// The high register should have the highest 32-bit part of the address
// The lower register should have the lowest 8-bit part
// This computation can be summarized as:
// res = addr + clamp(x) * num_comps
// Unfortunately, given the weird register division, we need some weird
// ops. We abuse some special functions from image addressing to reduce
// the number of instructions.
//
// For null descriptors, we put bit 31 high in lower_addr,
// this is then passed on bfm and sets the oob predicate high.
// And that's how we get safe null descriptors for free.
//
// TODO: check if we can also use vector instructions, codegen used VSHL
//       but it also didn't support sub-0x100 aligned buffers
static void
compute_buffer_address(nir_builder *b,
                       nir_intrinsic_instr *intrin,
                       enum glsl_sampler_dim sampler_dim,
                       nir_def **addr,
                       nir_def **is_oob)
{
   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);

   nir_def *num_elems = load_su_info(b, deref, clamp_x);
   nir_def *el_size_B = load_su_info(b, deref, pitch);
   nir_def *lower_addr = load_su_info(b, deref, extra);

   nir_def *raw_off = nir_channel(b, intrin->src[1].ssa, 0);

   *is_oob = nir_uge(b, raw_off, num_elems);

   nir_def *offset = nir_imad(b, raw_off, el_size_B, lower_addr);
   nir_def *base_addr = load_su_info(b, deref, addr_shifted8);

   *addr = nir_vec2(b, offset, base_addr);
}

static nir_def *
compute_address_from_ga_offset(nir_builder *b, nir_def *addr,
                               enum pipe_format format,
                               enum nak_su_ga_offset_mode offset_mode)
{
   assert(offset_mode == NAK_SUGA_OFF_MODE_U32 ||
          offset_mode == NAK_SUGA_OFF_MODE_U8);
   // mode U8: addr_hi contains bits 8..40, addr_lo contains 0..8
   // mode U32: addr_hi contains bits 8..40, addr_lo contains 0..32
   //           and they should be added to addr_hi
   nir_def *lo_8;
   nir_def *hi_32 = nir_channel(b, addr, 1);

   // With what should we fill the lower 8 bits?
   if (offset_mode == NAK_SUGA_OFF_MODE_U8) {
      lo_8 = nir_channel(b, addr, 0);
   } else {
      lo_8 = nir_imm_int(b, 0);
   }

   // Construct the 64-bit address (hi_32 << 8 | lo_8)
   nir_def *low = nir_prmt_nv(b, nir_imm_int(b, 0x6540), lo_8, hi_32);
   nir_def *high = nir_prmt_nv(b, nir_imm_int(b, 0x0007),
                                 nir_imm_int(b, 0), hi_32);
   nir_def *full_addr = nir_pack_64_2x32_split(b, low, high);

   if (offset_mode == NAK_SUGA_OFF_MODE_U32) {
      full_addr = nir_iadd(b, full_addr, nir_u2u64(b, nir_channel(b, addr, 0)));
   }

   return full_addr;
}

static void
lower_image_access(nir_builder *b, nir_intrinsic_instr *intrin)
{
   b->cursor = nir_instr_remove(&intrin->instr);
   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);

   enum pipe_format format = nir_intrinsic_format(intrin);

   if (format == PIPE_FORMAT_NONE)
      format = nir_deref_instr_get_variable(deref)->data.image.format;

   const unsigned int num_dst_components = intrin->def.num_components;
   const enum glsl_sampler_dim sampler_dim = nir_intrinsic_image_dim(intrin);
   enum nak_su_ga_offset_mode offset_mode;

   nir_def *addr, *is_oob;
   if (sampler_dim != GLSL_SAMPLER_DIM_BUF) {
      compute_image_address(b, intrin, sampler_dim, &addr, &is_oob);
      offset_mode = NAK_SUGA_OFF_MODE_U8;
   } else {
      compute_buffer_address(b, intrin, sampler_dim, &addr, &is_oob);
      offset_mode = NAK_SUGA_OFF_MODE_U32;
   }

   switch (intrin->intrinsic) {
   case nir_intrinsic_image_deref_load: {
      // .format intrinsic index is only used to pass how many bits to store
      nir_def *fmt = load_su_info(b, deref, format_info);
      nir_def *new_ssa = nir_suldga_nv(b, num_dst_components, addr, fmt,
                                       is_oob,
                                       .format = format,
                                       .access = nir_intrinsic_access(intrin),
                                       .flags = offset_mode);

      nir_intrinsic_instr *parent = nir_def_as_intrinsic(new_ssa);
      new_ssa = lower_formatted_image_load(b, parent, format);
      nir_def_rewrite_uses(&intrin->def, new_ssa);

      break;
   }
   case nir_intrinsic_image_deref_store: {
      nir_def *fmt = load_su_info(b, deref, format_info);
      nir_sustga_nv(b, addr, fmt, is_oob, intrin->src[3].ssa,
                    .access = nir_intrinsic_access(intrin),
                    .flags = offset_mode);
      break;
   }
   case nir_intrinsic_image_deref_atomic:
   case nir_intrinsic_image_deref_atomic_swap: {
      nir_atomic_op atomic_op = nir_intrinsic_atomic_op(intrin);

      // suldga and sustga expect address as [low_8, high_32]
      // while global_atomic expects a 64-bit address
      nir_def *full_addr = compute_address_from_ga_offset(b, addr,
                                                          format,
                                                          offset_mode);

      const unsigned bit_size = (format == PIPE_FORMAT_R64_UINT ||
                                 format == PIPE_FORMAT_R64_SINT) ? 64 : 32;

      nir_def *res, *res_ib, *res_oob;
      nir_push_if(b, nir_inot(b, is_oob));
      if (intrin->intrinsic == nir_intrinsic_image_deref_atomic) {
         res_ib = nir_global_atomic(b, bit_size, full_addr,
                                    intrin->src[3].ssa,
                                    .atomic_op = atomic_op);
      } else {
         res_ib = nir_global_atomic_swap(b, bit_size, full_addr,
                                         intrin->src[3].ssa,
                                         intrin->src[4].ssa,
                                         .atomic_op = atomic_op);
      }
      nir_push_else(b, NULL); {
         res_oob = nir_imm_intN_t(b, 0, bit_size);
      }
      nir_pop_if(b, NULL);
      res = nir_if_phi(b, res_ib, res_oob);

      nir_def_rewrite_uses(&intrin->def, res);
      break;
   }
   default:
      UNREACHABLE("Unknown image intrinsic");
   }
}

static void
lower_image_size(nir_builder *b, nir_intrinsic_instr *intrin)
{
   b->cursor = nir_instr_remove(&intrin->instr);
   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);

   const unsigned dim = sampler_dim_len(nir_intrinsic_image_dim(intrin));

   const bool is_array = nir_intrinsic_image_array(intrin);
   const unsigned cdim = dim + is_array;
   assert(cdim <= 3);
   nir_def *comps[3];

   nir_def *is_null = nir_ieq_imm(b, load_su_info(b, deref, addr_shifted8),
                                    0);
   nir_def *one_if_present = nir_bcsel(b, is_null, nir_imm_int(b, 0),
                                                   nir_imm_int(b, 1));

   // In descriptors we don't really have the size, but the clamp
   // since it's inclusive, it is size - 1.
   // Also, clamp is a bitfield, we need to extract the lower 16 bits.
   // We can do both operations (extraction and addition) with an imadsp.
   // To handle null descriptors, the addition accumulator of the imadsp
   // is 1 only for non-null descriptors.
   for (int i = 0; i < cdim; i++) {
      nir_def *clamp = load_su_info_clamp(b, deref, i);
      const struct nak_nir_imadsp_flags flags = {
         .src0 = NAK_IMAD_TYPE_U16_LO,
         .src1 = NAK_IMAD_TYPE_U24,
         .src2 = NAK_IMAD_TYPE_U16_LO,
         .params_from_src1 = false,
      };
      comps[i] = nir_imadsp_nv(b, clamp, one_if_present,
                               one_if_present,
                               .flags = NAK_AS_U32(flags));
   }
   // Clamp has multi-sampling already lowered, we need to de-lower it.
   if (nir_intrinsic_image_dim(intrin) == GLSL_SAMPLER_DIM_MS) {
      nir_def *ms_w_log2, *ms_h_log2;
      load_sample_size(b, deref, &ms_w_log2, &ms_h_log2);

      comps[0] = nir_ishr(b, comps[0], ms_w_log2);
      comps[1] = nir_ishr(b, comps[1], ms_h_log2);
   }

   nir_def_rewrite_uses(&intrin->def, nir_vec(b, comps, cdim));
}

static void
lower_buffer_size(nir_builder *b, nir_intrinsic_instr *intrin)
{
   b->cursor = nir_instr_remove(&intrin->instr);
   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
   nir_def *num_elems = load_su_info(b, deref, clamp_x);

   nir_def_rewrite_uses(&intrin->def, num_elems);
}

static void
lower_image_samples(nir_builder *b, nir_intrinsic_instr *intrin)
{
   b->cursor = nir_instr_remove(&intrin->instr);
   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);

   nir_def *sw_log2, *sh_log2;
   load_sample_size(b, deref, &sw_log2, &sh_log2);

   nir_def *samples_log2 = nir_iadd(b, sw_log2, sh_log2);
   nir_def *samples = nir_ishl(b, nir_imm_int(b, 1), samples_log2);

   // Handle null descriptors
   nir_def *addr = load_su_info(b, deref, addr_shifted8);
   nir_def *is_null = nir_ieq_imm(b, addr, 0);
   samples = nir_bcsel(b, is_null, nir_imm_int(b, 0), samples);

   nir_def_rewrite_uses(&intrin->def, samples);
}

static bool
lower_image_intrin(nir_builder *b,
                   nir_intrinsic_instr *intrin,
                   void *_data)
{
   switch (intrin->intrinsic) {
   case nir_intrinsic_image_deref_load:
   case nir_intrinsic_image_deref_store:
   case nir_intrinsic_image_deref_atomic:
   case nir_intrinsic_image_deref_atomic_swap:
      lower_image_access(b, intrin);
      return true;
   case nir_intrinsic_image_deref_size:
      if (nir_intrinsic_image_dim(intrin) == GLSL_SAMPLER_DIM_BUF) {
         lower_buffer_size(b, intrin);
      } else {
         lower_image_size(b, intrin);
      }
      return true;
   case nir_intrinsic_image_deref_samples:
      lower_image_samples(b, intrin);
      return true;
   default:
      return false;
   }
}

bool
nak_nir_lower_image_addrs(nir_shader *nir,
                          const struct nak_compiler *nak)
{
   return nir_shader_intrinsics_pass(nir, lower_image_intrin,
                                     nir_metadata_none,
                                     (void *)nak);
}
