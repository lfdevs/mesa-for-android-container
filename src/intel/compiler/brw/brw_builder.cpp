/*
 * Copyright © 2010 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_builder.h"

/*
 * This helper takes a source register and un/shuffles it into the destination
 * register.
 *
 * If source type size is smaller than destination type size the operation
 * needed is a component shuffle. The opposite case would be an unshuffle. If
 * source/destination type size is equal a shuffle is done that would be
 * equivalent to a simple MOV.
 *
 * For example, if source is a 16-bit type and destination is 32-bit. A 3
 * components .xyz 16-bit vector on SIMD8 would be.
 *
 *    |x1|x2|x3|x4|x5|x6|x7|x8|y1|y2|y3|y4|y5|y6|y7|y8|
 *    |z1|z2|z3|z4|z5|z6|z7|z8|  |  |  |  |  |  |  |  |
 *
 * This helper will return the following 2 32-bit components with the 16-bit
 * values shuffled:
 *
 *    |x1 y1|x2 y2|x3 y3|x4 y4|x5 y5|x6 y6|x7 y7|x8 y8|
 *    |z1   |z2   |z3   |z4   |z5   |z6   |z7   |z8   |
 *
 * For unshuffle, the example would be the opposite, a 64-bit type source
 * and a 32-bit destination. A 2 component .xy 64-bit vector on SIMD8
 * would be:
 *
 *    | x1l   x1h | x2l   x2h | x3l   x3h | x4l   x4h |
 *    | x5l   x5h | x6l   x6h | x7l   x7h | x8l   x8h |
 *    | y1l   y1h | y2l   y2h | y3l   y3h | y4l   y4h |
 *    | y5l   y5h | y6l   y6h | y7l   y7h | y8l   y8h |
 *
 * The returned result would be the following 4 32-bit components unshuffled:
 *
 *    | x1l | x2l | x3l | x4l | x5l | x6l | x7l | x8l |
 *    | x1h | x2h | x3h | x4h | x5h | x6h | x7h | x8h |
 *    | y1l | y2l | y3l | y4l | y5l | y6l | y7l | y8l |
 *    | y1h | y2h | y3h | y4h | y5h | y6h | y7h | y8h |
 *
 * - Source and destination register must not be overlapped.
 * - components units are measured in terms of the smaller type between
 *   source and destination because we are un/shuffling the smaller
 *   components from/into the bigger ones.
 * - first_component parameter allows skipping source components.
 */
static void
shuffle_src_to_dst(const brw_builder &bld,
                   const brw_reg &dst,
                   const brw_reg &src,
                   uint32_t first_component,
                   uint32_t components)
{
   if (brw_type_size_bytes(src.type) == brw_type_size_bytes(dst.type)) {
      assert(!regions_overlap(dst,
         brw_type_size_bytes(dst.type) * bld.dispatch_width() * components,
         offset(src, bld, first_component),
         brw_type_size_bytes(src.type) * bld.dispatch_width() * components));
      for (unsigned i = 0; i < components; i++) {
         bld.MOV(retype(offset(dst, bld, i), src.type),
                 offset(src, bld, i + first_component));
      }
   } else if (brw_type_size_bytes(src.type) < brw_type_size_bytes(dst.type)) {
      /* Source is shuffled into destination */
      unsigned size_ratio = brw_type_size_bytes(dst.type) / brw_type_size_bytes(src.type);
      assert(!regions_overlap(dst,
         brw_type_size_bytes(dst.type) * bld.dispatch_width() *
         DIV_ROUND_UP(components, size_ratio),
         offset(src, bld, first_component),
         brw_type_size_bytes(src.type) * bld.dispatch_width() * components));

      brw_reg_type shuffle_type =
         brw_type_with_size(BRW_TYPE_D, brw_type_size_bits(src.type));
      for (unsigned i = 0; i < components; i++) {
         brw_reg shuffle_component_i =
            subscript(offset(dst, bld, i / size_ratio),
                      shuffle_type, i % size_ratio);
         bld.MOV(shuffle_component_i,
                 retype(offset(src, bld, i + first_component), shuffle_type));
      }
   } else {
      /* Source is unshuffled into destination */
      unsigned size_ratio = brw_type_size_bytes(src.type) / brw_type_size_bytes(dst.type);
      assert(!regions_overlap(dst,
         brw_type_size_bytes(dst.type) * bld.dispatch_width() * components,
         offset(src, bld, first_component / size_ratio),
         brw_type_size_bytes(src.type) * bld.dispatch_width() *
         DIV_ROUND_UP(components + (first_component % size_ratio),
                      size_ratio)));

      brw_reg_type shuffle_type =
         brw_type_with_size(BRW_TYPE_D, brw_type_size_bits(dst.type));
      for (unsigned i = 0; i < components; i++) {
         brw_reg shuffle_component_i =
            subscript(offset(src, bld, (first_component + i) / size_ratio),
                      shuffle_type, (first_component + i) % size_ratio);
         bld.MOV(retype(offset(dst, bld, i), shuffle_type),
                 shuffle_component_i);
      }
   }
}

void
brw_builder::shuffle_from_32bit_read(const brw_reg &dst,
                                     const brw_reg &src,
                                     uint32_t first_component,
                                     uint32_t components) const
{
   assert(brw_type_size_bytes(src.type) == 4);

   /* This function takes components in units of the destination type while
    * shuffle_src_to_dst takes components in units of the smallest type
    */
   if (brw_type_size_bytes(dst.type) > 4) {
      assert(brw_type_size_bytes(dst.type) == 8);
      first_component *= 2;
      components *= 2;
   }

   shuffle_src_to_dst(*this, dst, src, first_component, components);
}

/**
 * Get the mask of SIMD channels enabled during dispatch and not yet disabled
 * by discard.  Due to the layout of the sample mask in the fragment shader
 * thread payload, \p bld is required to have a dispatch_width() not greater
 * than 16 for fragment shaders.
 */
brw_reg
brw_sample_mask_reg(const brw_builder &bld)
{
   const brw_shader &s = *bld.shader;

   if (s.stage != MESA_SHADER_FRAGMENT) {
      return brw_imm_ud(0xffffffff);
   } else if (s.devinfo->ver >= 20 ||
              brw_wm_prog_data(s.prog_data)->uses_kill) {
      return brw_flag_subreg(sample_mask_flag_subreg(s) + bld.group() / 16);
   } else {
      assert(bld.dispatch_width() <= 16);
      assert(s.devinfo->ver < 20);
      return retype(brw_vec1_grf((bld.group() >= 16 ? 2 : 1), 7),
                    BRW_TYPE_UW);
   }
}

/**
 * Predicate the specified instruction on the sample mask.
 */
void
brw_emit_predicate_on_sample_mask(const brw_builder &bld, brw_inst *inst)
{
   assert(bld.shader->stage == MESA_SHADER_FRAGMENT &&
          bld.group() == inst->group &&
          bld.dispatch_width() == inst->exec_size);

   const brw_shader &s = *bld.shader;
   const brw_reg sample_mask = brw_sample_mask_reg(bld);
   const unsigned subreg = sample_mask_flag_subreg(s);

   if (s.devinfo->ver >= 20 || brw_wm_prog_data(s.prog_data)->uses_kill) {
      assert(sample_mask.file == ARF &&
             sample_mask.nr == brw_flag_subreg(subreg).nr &&
             sample_mask.subnr == brw_flag_subreg(
                subreg + inst->group / 16).subnr);
   } else {
      bld.uniform().MOV(brw_flag_subreg(subreg + inst->group / 16),
                        sample_mask);
   }

   if (inst->predicate) {
      assert(inst->predicate == BRW_PREDICATE_NORMAL);
      assert(!inst->predicate_inverse);
      assert(inst->flag_subreg == 0);
      assert(s.devinfo->ver < 20);
      /* Combine the sample mask with the existing predicate by using a
       * vertical predication mode.
       */
      inst->predicate = BRW_PREDICATE_ALIGN1_ALLV;
   } else {
      inst->flag_subreg = subreg;
      inst->predicate = BRW_PREDICATE_NORMAL;
      inst->predicate_inverse = false;
   }
}


brw_reg
brw_fetch_payload_reg(const brw_builder &bld, uint8_t regs[2],
                      brw_reg_type type, unsigned n)
{
   if (!regs[0])
      return brw_reg();

   if (bld.dispatch_width() > 16) {
      const brw_reg tmp = bld.vgrf(type, n);
      const brw_builder hbld = bld.exec_all().group(16, 0);
      const unsigned m = bld.dispatch_width() / hbld.dispatch_width();
      brw_reg *const components = new brw_reg[m * n];

      for (unsigned c = 0; c < n; c++) {
         for (unsigned g = 0; g < m; g++)
            components[c * m + g] =
               offset(retype(brw_vec8_grf(regs[g], 0), type), hbld, c);
      }

      hbld.LOAD_PAYLOAD(tmp, components, m * n, 0);

      delete[] components;
      return tmp;

   } else {
      return brw_reg(retype(brw_vec8_grf(regs[0], 0), type));
   }
}

brw_reg
brw_fetch_barycentric_reg(const brw_builder &bld, uint8_t regs[2])
{
   if (!regs[0])
      return brw_reg();
   else if (bld.shader->devinfo->ver >= 20)
      return brw_fetch_payload_reg(bld, regs, BRW_TYPE_F, 2);

   const brw_reg tmp = bld.vgrf(BRW_TYPE_F, 2);
   const brw_builder hbld = bld.exec_all().group(8, 0);
   const unsigned m = bld.dispatch_width() / hbld.dispatch_width();
   brw_reg *const components = new brw_reg[2 * m];

   for (unsigned c = 0; c < 2; c++) {
      for (unsigned g = 0; g < m; g++)
         components[c * m + g] = offset(brw_vec8_grf(regs[g / 2], 0),
                                        hbld, c + 2 * (g % 2));
   }

   hbld.LOAD_PAYLOAD(tmp, components, 2 * m, 0);

   delete[] components;
   return tmp;
}

void
brw_check_dynamic_msaa_flag(const brw_builder &bld,
                        const struct brw_wm_prog_data *wm_prog_data,
                        enum intel_msaa_flags flag)
{
   brw_inst *inst = bld.AND(bld.null_reg_ud(),
                            brw_dynamic_msaa_flags(wm_prog_data),
                            brw_imm_ud(flag));
   inst->conditional_mod = BRW_CONDITIONAL_NZ;
}

