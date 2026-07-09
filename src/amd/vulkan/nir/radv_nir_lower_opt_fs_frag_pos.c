/* Copyright © 2026 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

/* Lower frag_coord_xy to pixel_coord if either force_pixel_coord=true or all uses of all instances
 * of frag_coord_xy ignore the fractional part, and lower pixel_coord to frag_coord_xy if both
 * pixel_coord and frag_coord_xy exist and frag_coord_xy has at least one use that doesn't ignore
 * the fractional part. At the end of the pass, only frag_coord_xy or pixel_coord can be present,
 * not both.
 *
 * sample_pos counts as a frag_coord_xy use and is lowered to frag_coord_xy here.
 *
 * If any component has only integer uses with only bit 0 used, such as (pixel_coord & 0x1) or
 * (f2u32(frag_coord_xy) & 0x1), which indicates that the shader does screen-space patterning, then
 * we can take advantage of the fact that quads always start on even screen coordinates, meaning that
 * bit 0 of pixel_coord is always present in the first 2 bits of the subgroup invocation ID, which
 * completely eliminates the original system values. Other passes optimize it to constant lane masks
 * if possible. The replacement:
 *
 *    pixel_coord.x = subgroup_invocation & 0x1;
 *    pixel_coord.y = (subgroup_invocation >> 1) & 0x1;
 *
 *    If dynamic VRS disallows that replacement, load_use_quad_pos_amd is used to do it conditionally.
 *    The pixel_coord VGPR initialization is disabled if load_use_quad_pos_amd==true. The main reason
 *    we do this is because VRS is always dynamic state in DX12, which means this is the only path
 *    that can use quad_pos through DX12:
 *
 *    pixel_coord.x = load_use_quad_pos_amd ? subgroup_invocation & 0x1 : pixel_coord.x;
 *    pixel_coord.y = load_use_quad_pos_amd ? (subgroup_invocation >> 1) & 0x1 : pixel_coord.y;
 *
 * If frag_coord_xy survives and both components are used and sample_pos isn't used, it becomes:
 *    load_use_float_frag_coord_xy_amd() ? load_frag_coord_xy() : u2f32(load_pixel_coord()) + 0.5;
 *
 *    If load_use_float_frag_coord_xy_amd==false, load_frag_coord_xy() returns uninitialized values.
 *    If load_use_float_frag_coord_xy_amd==true, load_pixel_coord() returns uninitialized values.
 *
 *    SPI_PS_INPUT_ENA is used to disable VGPR initialization for frag_coord_xy (POS_X_FLOAT,
 *    POS_Y_FLOAT) or pixel_coord (POS_FIXED_PT) while SPI_PS_INPUT_ADDR keeps them at the same
 *    VGPR locations. Reducing the number of initialized VGPRs increases the PS wave launch rate,
 *    which increases observed pixel throughput depending on other states.
 *
 *    load_use_float_frag_coord_xy_amd() comes from a user SGPR, and determines which VGPRs are
 *    initialized at PS wave launch.
 */

#include "nir_builder.h"
#include "nir_range_analysis.h"
#include "radv_nir.h"
#include "radv_shader_info.h"

typedef struct {
   /* gather_fs_frag_pos */
   nir_component_mask_t frag_pos_float_uses;
   nir_component_mask_t frag_pos_integer_uses;
   nir_component_mask_t frag_pos_only_bit0_uses;

   bool has_frag_coord_xy;
   bool has_pixel_coord;
   bool has_sample_pos;

   /* lower_fs_frag_pos */
   bool lower_to_pixel_coord;
   bool lower_to_frag_coord_xy;
   bool select_frag_coord_xy_dynamically;
   uint8_t dynamic_quad_pos_comp_mask;
} opt_fs_frag_coord_and_pixel_coord_state;

static bool
gather_fs_frag_pos(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   opt_fs_frag_coord_and_pixel_coord_state *state = (opt_fs_frag_coord_and_pixel_coord_state *)data;

   switch (intr->intrinsic) {
   case nir_intrinsic_load_frag_coord:
      UNREACHABLE("only frag_coord_xy is expected");

   case nir_intrinsic_load_frag_coord_xy:
   case nir_intrinsic_load_sample_pos: {
      assert(!nir_def_is_unused(&intr->def));

      nir_component_mask_t integer_uses = 0;
      uint64_t integer_bits_used[NIR_MAX_VEC_COMPONENTS];
      integer_bits_used[0] = 0;
      integer_bits_used[1] = 0;

      nir_gather_type_uses_of_float_def(&intr->def, &state->frag_pos_float_uses, &integer_uses, integer_bits_used,
                                        false);
      uint8_t only_bit0_uses = (integer_bits_used[0] == 0x1) | ((integer_bits_used[1] == 0x1) << 1);

      state->has_frag_coord_xy |= intr->intrinsic == nir_intrinsic_load_frag_coord_xy;
      state->has_sample_pos |= intr->intrinsic == nir_intrinsic_load_sample_pos;
      state->frag_pos_only_bit0_uses |= only_bit0_uses;
      /* We shouldn't treat bit 0 uses as integer uses because integer_uses implies that there is
       * at least one non-bit 0 use.
       */
      state->frag_pos_integer_uses |= integer_uses & ~only_bit0_uses;
      return false;
   }

   case nir_intrinsic_load_pixel_coord: {
      uint8_t comp_mask = nir_def_components_read(&intr->def);
      uint8_t only_bit0_uses =
         (comp_mask & 0x1 ? nir_def_bits_used(nir_scalar_resolved(&intr->def, 0)) == 0x1 : 0) |
         ((comp_mask & 0x2 ? nir_def_bits_used(nir_scalar_resolved(&intr->def, 1)) == 0x1 : 0) << 1);

      state->has_pixel_coord = true;
      state->frag_pos_integer_uses |= comp_mask;
      state->frag_pos_only_bit0_uses |= only_bit0_uses;
      return false;
   }

   default:
      return false;
   }
}

/* Load the position of the current invocation in a fragment quad (2x2).
 * VRS must be disabled if this is used to get bit 0 of pixel_coord because VRS changes the size
 * of fragment quads.
 *
 * This exploits 2 facts:
 * - quads always start on even screen coordinates
 * - invocations within a quad are always in the same order
 */
static nir_def *
get_quad_pos(nir_builder *b, bool is_float)
{
   nir_def *invoc_id = nir_u2uN(b, nir_load_subgroup_invocation(b), is_float ? 32 : 16);

   /* quad_pos.x = subgroup_invocation & 0x1;
    * quad_pos.y = (subgroup_invocation >> 1) & 0x1;
    */
   nir_def *quad_pos = nir_iand_imm(b, nir_vec2(b, invoc_id, nir_ushr_imm(b, invoc_id, 1)), 0x1);

   return is_float ? nir_u2f32(b, quad_pos) : quad_pos;
}

static nir_def *
select_frag_pos_bit0_or_sysval(opt_fs_frag_coord_and_pixel_coord_state *state, nir_builder *b, nir_def *sysval,
                               bool is_float)
{
   assert(state->frag_pos_only_bit0_uses || state->dynamic_quad_pos_comp_mask);
   uint8_t mask =
      state->dynamic_quad_pos_comp_mask ? state->dynamic_quad_pos_comp_mask : state->frag_pos_only_bit0_uses;
   nir_def *cond = nir_vec2(b, nir_imm_bool(b, !!(mask & 0x1)), nir_imm_bool(b, !!(mask & 0x2)));

   if (state->dynamic_quad_pos_comp_mask)
      cond = nir_iand(b, cond, nir_load_use_quad_pos_amd(b));

   return nir_bcsel(b, cond, get_quad_pos(b, is_float), sysval);
}

static bool
lower_fs_frag_pos(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   opt_fs_frag_coord_and_pixel_coord_state *state = (opt_fs_frag_coord_and_pixel_coord_state *)data;

   /* frag_pos_only_bit0_uses isn't beneficial with these other flags. */
   assert(!state->frag_pos_only_bit0_uses || !state->lower_to_pixel_coord);
   assert(!state->frag_pos_only_bit0_uses || !state->select_frag_coord_xy_dynamically);
   assert(!state->frag_pos_only_bit0_uses || !state->has_sample_pos);
   assert(!state->frag_pos_only_bit0_uses || !state->dynamic_quad_pos_comp_mask);
   assert(!state->dynamic_quad_pos_comp_mask || !state->select_frag_coord_xy_dynamically);

   b->cursor = nir_before_instr(&intr->instr);

   switch (intr->intrinsic) {
   case nir_intrinsic_load_frag_coord_xy:
      if (state->lower_to_pixel_coord) {
         nir_def *float_pixel_coord = nir_u2f32(b, nir_load_pixel_coord(b));

         if (state->dynamic_quad_pos_comp_mask)
            float_pixel_coord = select_frag_pos_bit0_or_sysval(state, b, float_pixel_coord, true);

         nir_def_replace(&intr->def, nir_fadd_imm(b, float_pixel_coord, 0.5));
         return true;
      } else if (state->select_frag_coord_xy_dynamically) {
         nir_def_replace(&intr->def, nir_bcsel(b, nir_load_use_float_frag_coord_xy_amd(b), nir_load_frag_coord_xy(b),
                                               nir_fadd_imm(b, nir_u2f32(b, nir_load_pixel_coord(b)), 0.5)));
         return true;
      } else if (state->frag_pos_only_bit0_uses) {
         nir_def_replace(&intr->def, select_frag_pos_bit0_or_sysval(state, b, nir_load_frag_coord_xy(b), true));
         return true;
      }

      /* quad_pos requires a non-float use. */
      assert(!state->dynamic_quad_pos_comp_mask);
      return false;

   case nir_intrinsic_load_sample_pos:
      if (state->lower_to_pixel_coord) {
         /* This is unlikely and only possible with integer use. */
         nir_def_replace(&intr->def, nir_imm_vec2(b, 0, 0));
      } else {
         nir_def_replace(&intr->def, nir_ffract(b, nir_load_frag_coord_xy(b)));
         assert(!state->dynamic_quad_pos_comp_mask);
      }
      return true;

   case nir_intrinsic_load_pixel_coord:
      if (state->lower_to_frag_coord_xy) {
         if (state->select_frag_coord_xy_dynamically) {
            nir_def_replace(&intr->def, nir_bcsel(b, nir_load_use_float_frag_coord_xy_amd(b),
                                                  nir_f2u16(b, nir_load_frag_coord_xy(b)), nir_load_pixel_coord(b)));
         } else if (state->frag_pos_only_bit0_uses || state->dynamic_quad_pos_comp_mask) {
            nir_def_replace(&intr->def,
                            select_frag_pos_bit0_or_sysval(state, b, nir_f2u16(b, nir_load_frag_coord_xy(b)), false));
         } else {
            nir_def_replace(&intr->def, nir_f2u16(b, nir_load_frag_coord_xy(b)));
         }
         return true;
      } else if (state->frag_pos_only_bit0_uses == 0x3) {
         /* Replacing only one component isn't beneficial since pixel_coord gives us the second
          * component for free, which is why we replace either both components or neither.
          */
         nir_def_replace(&intr->def, get_quad_pos(b, false));
         return true;
      }
      return false;

   default:
      return false;
   }
}

bool
radv_nir_lower_opt_fs_frag_pos(nir_shader *shader, bool vrs_may_be_enabled, bool sample_shading)
{
   opt_fs_frag_coord_and_pixel_coord_state state = {0};
   bool always_at_pixel_center = !vrs_may_be_enabled && !sample_shading;

   /* Even with always_at_pixel_center, we still need to gather which components only use bit 0 of
    * integer.
    */
   nir_shader_intrinsics_pass(shader, gather_fs_frag_pos, nir_metadata_all, &state);

   if (vrs_may_be_enabled) {
      /* VRS changes the size of fragment quads, which means we can't use the subgroup invocation ID
       * to get bit 0 of pixel_coord unconditionally.
       */
      state.dynamic_quad_pos_comp_mask = state.frag_pos_only_bit0_uses;
      state.frag_pos_integer_uses |= state.frag_pos_only_bit0_uses;
      state.frag_pos_only_bit0_uses = 0;
   }

   /* If only one component is used as integer and no component is used as float, the choice of
    * float or integer doesn't change the number of initialized PS VGPRs, but we can eliminate
    * the conversion from float to integer if we lower to pixel_coord.
    */
   bool only_one_comp_used_as_integer = !state.frag_pos_float_uses && util_bitcount(state.frag_pos_integer_uses) == 1 &&
                                        !state.frag_pos_only_bit0_uses;

   if (always_at_pixel_center) {
      /* fragcoord_xy will be lowered to pixel_coord + 0.5 when it's beneficial. */
      state.frag_pos_integer_uses |= state.frag_pos_float_uses;
      state.frag_pos_float_uses = 0;
   }

   /* Make the masks disjoint. */
   state.frag_pos_only_bit0_uses &= ~(state.frag_pos_float_uses | state.frag_pos_integer_uses);
   state.frag_pos_integer_uses &= ~state.frag_pos_float_uses;

   /* Lower frag_coord_xy and sample_pos to pixel_coord only if both components are used as integer
    * (because it doesn't reduce initialized PS VGPRs if only one component is used) or if there is
    * only one used component and it's integer (starting from pixel_coord allows us to remove
    * the conversion to integer, and that's the only benefit).
    */
   state.lower_to_pixel_coord = (state.has_frag_coord_xy || state.has_sample_pos) &&
                                (state.frag_pos_integer_uses == 0x3 || only_one_comp_used_as_integer);

   /* Lower to frag_coord_xy only if we have pixel_coord or sample_pos and at least one component
    * of frag_coord_xy or sample_pos is used as float (implied by having a float use).
    *
    * This also lowers pixel_coord to frag_coord_xy because we don't want both to be present, and
    * it also lowers sample_pos to ffract(frag_coord_xy).
    */
   state.lower_to_frag_coord_xy = (state.has_pixel_coord || state.has_sample_pos) && state.frag_pos_float_uses;

   /* Only select frag_coord_xy dynamically if both components are used and at least one component
    * is used as float. We don't do this if only 1 component is used because it wouldn't reduce
    * initialized PS VGPRs.
    *
    * At runtime, the driver will set a bit in a user SGPR determining whether frag_coord_xy is
    * always equal to the pixel center to make the shader select (pixel_coord + 0.5). When that
    * happens, the frag_coord_xy VGPR initalization will be disabled.
    *
    * We don't do this if sample_pos is present because sample_pos implies sample shading, which is
    * unlikely to have a single-sample framebuffer or all sample positions at the pixel center.
    */
   state.select_frag_coord_xy_dynamically = !state.has_sample_pos && state.frag_pos_float_uses &&
                                            (state.frag_pos_float_uses | state.frag_pos_integer_uses) == 0x3;

   /* Don't select quad_pos dynamically if it doesn't completely remove pixel_coord since that
    * would only add ALU with no benefit.
    */
   if (state.dynamic_quad_pos_comp_mask && state.dynamic_quad_pos_comp_mask != state.frag_pos_integer_uses)
      state.dynamic_quad_pos_comp_mask = 0;

   if (!state.frag_pos_only_bit0_uses && !state.lower_to_pixel_coord && !state.lower_to_frag_coord_xy &&
       !state.select_frag_coord_xy_dynamically && !state.dynamic_quad_pos_comp_mask)
      return false;

   return nir_shader_intrinsics_pass(shader, lower_fs_frag_pos, nir_metadata_control_flow, &state);
}
