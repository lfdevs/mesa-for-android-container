/*
 * Copyright © 2010 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_eu.h"
#include "brw_fs.h"
#include "brw_fs_builder.h"
#include "brw_fs_live_variables.h"
#include "brw_nir.h"
#include "brw_cfg.h"
#include "brw_private.h"
#include "intel_nir.h"
#include "shader_enums.h"
#include "dev/intel_debug.h"
#include "dev/intel_wa.h"

#include <memory>

using namespace brw;

static fs_inst *
brw_emit_single_fb_write(fs_visitor &s, const fs_builder &bld,
                         brw_reg color0, brw_reg color1,
                         brw_reg src0_alpha, unsigned components,
                         bool null_rt)
{
   assert(s.stage == MESA_SHADER_FRAGMENT);
   struct brw_wm_prog_data *prog_data = brw_wm_prog_data(s.prog_data);

   /* Hand over gl_FragDepth or the payload depth. */
   const brw_reg dst_depth = fetch_payload_reg(bld, s.fs_payload().dest_depth_reg);

   brw_reg sources[FB_WRITE_LOGICAL_NUM_SRCS];
   sources[FB_WRITE_LOGICAL_SRC_COLOR0]     = color0;
   sources[FB_WRITE_LOGICAL_SRC_COLOR1]     = color1;
   sources[FB_WRITE_LOGICAL_SRC_SRC0_ALPHA] = src0_alpha;
   sources[FB_WRITE_LOGICAL_SRC_DST_DEPTH]  = dst_depth;
   sources[FB_WRITE_LOGICAL_SRC_COMPONENTS] = brw_imm_ud(components);
   sources[FB_WRITE_LOGICAL_SRC_NULL_RT]    = brw_imm_ud(null_rt);

   if (prog_data->uses_omask)
      sources[FB_WRITE_LOGICAL_SRC_OMASK] = s.sample_mask;
   if (s.nir->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_DEPTH))
      sources[FB_WRITE_LOGICAL_SRC_SRC_DEPTH] = s.frag_depth;
   if (s.nir->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_STENCIL))
      sources[FB_WRITE_LOGICAL_SRC_SRC_STENCIL] = s.frag_stencil;

   fs_inst *write = bld.emit(FS_OPCODE_FB_WRITE_LOGICAL, brw_reg(),
                             sources, ARRAY_SIZE(sources));

   if (prog_data->uses_kill) {
      write->predicate = BRW_PREDICATE_NORMAL;
      write->flag_subreg = sample_mask_flag_subreg(s);
   }

   return write;
}

static void
brw_do_emit_fb_writes(fs_visitor &s, int nr_color_regions, bool replicate_alpha)
{
   const fs_builder bld = fs_builder(&s).at_end();
   fs_inst *inst = NULL;

   for (int target = 0; target < nr_color_regions; target++) {
      /* Skip over outputs that weren't written. */
      if (s.outputs[target].file == BAD_FILE)
         continue;

      const fs_builder abld = bld.annotate(
         ralloc_asprintf(s.mem_ctx, "FB write target %d", target));

      brw_reg src0_alpha;
      if (replicate_alpha && target != 0)
         src0_alpha = offset(s.outputs[0], bld, 3);

      inst = brw_emit_single_fb_write(s, abld, s.outputs[target],
                                      s.dual_src_output, src0_alpha, 4,
                                      false);
      inst->target = target;
   }

   if (inst == NULL) {
      struct brw_wm_prog_key *key = (brw_wm_prog_key*) s.key;
      struct brw_wm_prog_data *prog_data = brw_wm_prog_data(s.prog_data);
      /* Disable null_rt if any non color output is written or if
       * alpha_to_coverage can be enabled. Since the alpha_to_coverage bit is
       * coming from the BLEND_STATE structure and the HW will avoid reading
       * it if null_rt is enabled.
       */
      const bool use_null_rt =
         key->alpha_to_coverage == BRW_NEVER &&
         !prog_data->uses_omask;

      /* Even if there's no color buffers enabled, we still need to send
       * alpha out the pipeline to our null renderbuffer to support
       * alpha-testing, alpha-to-coverage, and so on.
       */
      /* FINISHME: Factor out this frequently recurring pattern into a
       * helper function.
       */
      const brw_reg srcs[] = { reg_undef, reg_undef,
                              reg_undef, offset(s.outputs[0], bld, 3) };
      const brw_reg tmp = bld.vgrf(BRW_TYPE_UD, 4);
      bld.LOAD_PAYLOAD(tmp, srcs, 4, 0);

      inst = brw_emit_single_fb_write(s, bld, tmp, reg_undef, reg_undef, 4,
                                      use_null_rt);
      inst->target = 0;
   }

   inst->last_rt = true;
   inst->eot = true;
}

static void
brw_emit_fb_writes(fs_visitor &s)
{
   const struct intel_device_info *devinfo = s.devinfo;
   assert(s.stage == MESA_SHADER_FRAGMENT);
   struct brw_wm_prog_data *prog_data = brw_wm_prog_data(s.prog_data);
   brw_wm_prog_key *key = (brw_wm_prog_key*) s.key;

   if (s.nir->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_STENCIL)) {
      /* From the 'Render Target Write message' section of the docs:
       * "Output Stencil is not supported with SIMD16 Render Target Write
       * Messages."
       */
      if (devinfo->ver >= 20)
         s.limit_dispatch_width(16, "gl_FragStencilRefARB unsupported "
                                "in SIMD32+ mode.\n");
      else
         s.limit_dispatch_width(8, "gl_FragStencilRefARB unsupported "
                                "in SIMD16+ mode.\n");
   }

   /* ANV doesn't know about sample mask output during the wm key creation
    * so we compute if we need replicate alpha and emit alpha to coverage
    * workaround here.
    */
   const bool replicate_alpha = key->alpha_test_replicate_alpha ||
      (key->nr_color_regions > 1 && key->alpha_to_coverage &&
       s.sample_mask.file == BAD_FILE);

   prog_data->dual_src_blend = (s.dual_src_output.file != BAD_FILE &&
                                s.outputs[0].file != BAD_FILE);
   assert(!prog_data->dual_src_blend || key->nr_color_regions == 1);

   /* Following condition implements Wa_14017468336:
    *
    * "If dual source blend is enabled do not enable SIMD32 dispatch" and
    * "For a thread dispatched as SIMD32, must not issue SIMD8 message with Last
    *  Render Target Select set."
    */
   if (devinfo->ver >= 11 && devinfo->ver <= 12 &&
       prog_data->dual_src_blend) {
      /* The dual-source RT write messages fail to release the thread
       * dependency on ICL and TGL with SIMD32 dispatch, leading to hangs.
       *
       * XXX - Emit an extra single-source NULL RT-write marked LastRT in
       *       order to release the thread dependency without disabling
       *       SIMD32.
       *
       * The dual-source RT write messages may lead to hangs with SIMD16
       * dispatch on ICL due some unknown reasons, see
       * https://gitlab.freedesktop.org/mesa/mesa/-/issues/2183
       */
      if (devinfo->ver >= 20)
         s.limit_dispatch_width(16, "Dual source blending unsupported "
                                "in SIMD32 mode.\n");
      else
         s.limit_dispatch_width(8, "Dual source blending unsupported "
                                "in SIMD16 and SIMD32 modes.\n");
   }

   brw_do_emit_fb_writes(s, key->nr_color_regions, replicate_alpha);
}


/** Emits the interpolation for the varying inputs. */
static void
brw_emit_interpolation_setup(fs_visitor &s)
{
   const struct intel_device_info *devinfo = s.devinfo;
   const fs_builder bld = fs_builder(&s).at_end();
   fs_builder abld = bld.annotate("compute pixel centers");

   s.pixel_x = bld.vgrf(BRW_TYPE_F);
   s.pixel_y = bld.vgrf(BRW_TYPE_F);

   const struct brw_wm_prog_key *wm_key = (brw_wm_prog_key*) s.key;
   struct brw_wm_prog_data *wm_prog_data = brw_wm_prog_data(s.prog_data);
   fs_thread_payload &payload = s.fs_payload();

   brw_reg int_sample_offset_x, int_sample_offset_y; /* Used on Gen12HP+ */
   brw_reg int_sample_offset_xy; /* Used on Gen8+ */
   brw_reg half_int_sample_offset_x, half_int_sample_offset_y;
   if (wm_prog_data->coarse_pixel_dispatch != BRW_ALWAYS) {
      /* The thread payload only delivers subspan locations (ss0, ss1,
       * ss2, ...). Since subspans covers 2x2 pixels blocks, we need to
       * generate 4 pixel coordinates out of each subspan location. We do this
       * by replicating a subspan coordinate 4 times and adding an offset of 1
       * in each direction from the initial top left (tl) location to generate
       * top right (tr = +1 in x), bottom left (bl = +1 in y) and bottom right
       * (br = +1 in x, +1 in y).
       *
       * The locations we build look like this in SIMD8 :
       *
       *    ss0.tl ss0.tr ss0.bl ss0.br ss1.tl ss1.tr ss1.bl ss1.br
       *
       * The value 0x11001010 is a vector of 8 half byte vector. It adds
       * following to generate the 4 pixels coordinates out of the subspan0:
       *
       *  0x
       *    1 : ss0.y + 1 -> ss0.br.y
       *    1 : ss0.y + 1 -> ss0.bl.y
       *    0 : ss0.y + 0 -> ss0.tr.y
       *    0 : ss0.y + 0 -> ss0.tl.y
       *    1 : ss0.x + 1 -> ss0.br.x
       *    0 : ss0.x + 0 -> ss0.bl.x
       *    1 : ss0.x + 1 -> ss0.tr.x
       *    0 : ss0.x + 0 -> ss0.tl.x
       *
       * By doing a SIMD16 add in a SIMD8 shader, we can generate the 8 pixels
       * coordinates out of 2 subspans coordinates in a single ADD instruction
       * (twice the operation above).
       */
      int_sample_offset_xy = brw_reg(brw_imm_v(0x11001010));
      half_int_sample_offset_x = brw_reg(brw_imm_uw(0));
      half_int_sample_offset_y = brw_reg(brw_imm_uw(0));
      /* On Gfx12.5, because of regioning restrictions, the interpolation code
       * is slightly different and works off X & Y only inputs. The ordering
       * of the half bytes here is a bit odd, with each subspan replicated
       * twice and every other element is discarded :
       *
       *             ss0.tl ss0.tl ss0.tr ss0.tr ss0.bl ss0.bl ss0.br ss0.br
       *  X offset:    0      0      1      0      0      0      1      0
       *  Y offset:    0      0      0      0      1      0      1      0
       */
      int_sample_offset_x = brw_reg(brw_imm_v(0x01000100));
      int_sample_offset_y = brw_reg(brw_imm_v(0x01010000));
   }

   brw_reg int_coarse_offset_x, int_coarse_offset_y; /* Used on Gen12HP+ */
   brw_reg int_coarse_offset_xy; /* Used on Gen8+ */
   brw_reg half_int_coarse_offset_x, half_int_coarse_offset_y;
   if (wm_prog_data->coarse_pixel_dispatch != BRW_NEVER) {
      /* In coarse pixel dispatch we have to do the same ADD instruction that
       * we do in normal per pixel dispatch, except this time we're not adding
       * 1 in each direction, but instead the coarse pixel size.
       *
       * The coarse pixel size is delivered as 2 u8 in r1.0
       */
      struct brw_reg r1_0 = retype(brw_vec1_reg(FIXED_GRF, 1, 0), BRW_TYPE_UB);

      const fs_builder dbld =
         abld.exec_all().group(MIN2(16, s.dispatch_width) * 2, 0);

      if (devinfo->verx10 >= 125) {
         /* To build the array of half bytes we do and AND operation with the
          * right mask in X.
          */
         int_coarse_offset_x = dbld.vgrf(BRW_TYPE_UW);
         dbld.AND(int_coarse_offset_x, byte_offset(r1_0, 0), brw_imm_v(0x0f000f00));

         /* And the right mask in Y. */
         int_coarse_offset_y = dbld.vgrf(BRW_TYPE_UW);
         dbld.AND(int_coarse_offset_y, byte_offset(r1_0, 1), brw_imm_v(0x0f0f0000));
      } else {
         /* To build the array of half bytes we do and AND operation with the
          * right mask in X.
          */
         int_coarse_offset_x = dbld.vgrf(BRW_TYPE_UW);
         dbld.AND(int_coarse_offset_x, byte_offset(r1_0, 0), brw_imm_v(0x0000f0f0));

         /* And the right mask in Y. */
         int_coarse_offset_y = dbld.vgrf(BRW_TYPE_UW);
         dbld.AND(int_coarse_offset_y, byte_offset(r1_0, 1), brw_imm_v(0xff000000));

         /* Finally OR the 2 registers. */
         int_coarse_offset_xy = dbld.vgrf(BRW_TYPE_UW);
         dbld.OR(int_coarse_offset_xy, int_coarse_offset_x, int_coarse_offset_y);
      }

      /* Also compute the half coarse size used to center coarses. */
      half_int_coarse_offset_x = bld.vgrf(BRW_TYPE_UW);
      half_int_coarse_offset_y = bld.vgrf(BRW_TYPE_UW);

      bld.SHR(half_int_coarse_offset_x, suboffset(r1_0, 0), brw_imm_ud(1));
      bld.SHR(half_int_coarse_offset_y, suboffset(r1_0, 1), brw_imm_ud(1));
   }

   brw_reg int_pixel_offset_x, int_pixel_offset_y; /* Used on Gen12HP+ */
   brw_reg int_pixel_offset_xy; /* Used on Gen8+ */
   brw_reg half_int_pixel_offset_x, half_int_pixel_offset_y;
   switch (wm_prog_data->coarse_pixel_dispatch) {
   case BRW_NEVER:
      int_pixel_offset_x = int_sample_offset_x;
      int_pixel_offset_y = int_sample_offset_y;
      int_pixel_offset_xy = int_sample_offset_xy;
      half_int_pixel_offset_x = half_int_sample_offset_x;
      half_int_pixel_offset_y = half_int_sample_offset_y;
      break;

   case BRW_SOMETIMES: {
      const fs_builder dbld =
         abld.exec_all().group(MIN2(16, s.dispatch_width) * 2, 0);

      check_dynamic_msaa_flag(dbld, wm_prog_data,
                              INTEL_MSAA_FLAG_COARSE_RT_WRITES);

      int_pixel_offset_x = dbld.vgrf(BRW_TYPE_UW);
      set_predicate(BRW_PREDICATE_NORMAL,
                    dbld.SEL(int_pixel_offset_x,
                             int_coarse_offset_x,
                             int_sample_offset_x));

      int_pixel_offset_y = dbld.vgrf(BRW_TYPE_UW);
      set_predicate(BRW_PREDICATE_NORMAL,
                    dbld.SEL(int_pixel_offset_y,
                             int_coarse_offset_y,
                             int_sample_offset_y));

      int_pixel_offset_xy = dbld.vgrf(BRW_TYPE_UW);
      set_predicate(BRW_PREDICATE_NORMAL,
                    dbld.SEL(int_pixel_offset_xy,
                             int_coarse_offset_xy,
                             int_sample_offset_xy));

      half_int_pixel_offset_x = bld.vgrf(BRW_TYPE_UW);
      set_predicate(BRW_PREDICATE_NORMAL,
                    bld.SEL(half_int_pixel_offset_x,
                            half_int_coarse_offset_x,
                            half_int_sample_offset_x));

      half_int_pixel_offset_y = bld.vgrf(BRW_TYPE_UW);
      set_predicate(BRW_PREDICATE_NORMAL,
                    bld.SEL(half_int_pixel_offset_y,
                            half_int_coarse_offset_y,
                            half_int_sample_offset_y));
      break;
   }

   case BRW_ALWAYS:
      int_pixel_offset_x = int_coarse_offset_x;
      int_pixel_offset_y = int_coarse_offset_y;
      int_pixel_offset_xy = int_coarse_offset_xy;
      half_int_pixel_offset_x = half_int_coarse_offset_x;
      half_int_pixel_offset_y = half_int_coarse_offset_y;
      break;
   }

   for (unsigned i = 0; i < DIV_ROUND_UP(s.dispatch_width, 16); i++) {
      const fs_builder hbld = abld.group(MIN2(16, s.dispatch_width), i);
      /* According to the "PS Thread Payload for Normal Dispatch"
       * pages on the BSpec, subspan X/Y coordinates are stored in
       * R1.2-R1.5/R2.2-R2.5 on gfx6+, and on R0.10-R0.13/R1.10-R1.13
       * on gfx20+.  gi_reg is the 32B section of the GRF that
       * contains the subspan coordinates.
       */
      const struct brw_reg gi_reg = devinfo->ver >= 20 ? xe2_vec1_grf(i, 8) :
                                    brw_vec1_grf(i + 1, 0);
      const struct brw_reg gi_uw = retype(gi_reg, BRW_TYPE_UW);

      if (devinfo->verx10 >= 125) {
         const fs_builder dbld =
            abld.exec_all().group(hbld.dispatch_width() * 2, 0);
         const brw_reg int_pixel_x = dbld.vgrf(BRW_TYPE_UW);
         const brw_reg int_pixel_y = dbld.vgrf(BRW_TYPE_UW);

         dbld.ADD(int_pixel_x,
                  brw_reg(stride(suboffset(gi_uw, 4), 2, 8, 0)),
                  int_pixel_offset_x);
         dbld.ADD(int_pixel_y,
                  brw_reg(stride(suboffset(gi_uw, 5), 2, 8, 0)),
                  int_pixel_offset_y);

         if (wm_prog_data->coarse_pixel_dispatch != BRW_NEVER) {
            fs_inst *addx = dbld.ADD(int_pixel_x, int_pixel_x,
                                     horiz_stride(half_int_pixel_offset_x, 0));
            fs_inst *addy = dbld.ADD(int_pixel_y, int_pixel_y,
                                     horiz_stride(half_int_pixel_offset_y, 0));
            if (wm_prog_data->coarse_pixel_dispatch != BRW_ALWAYS) {
               addx->predicate = BRW_PREDICATE_NORMAL;
               addy->predicate = BRW_PREDICATE_NORMAL;
            }
         }

         hbld.MOV(offset(s.pixel_x, hbld, i), horiz_stride(int_pixel_x, 2));
         hbld.MOV(offset(s.pixel_y, hbld, i), horiz_stride(int_pixel_y, 2));

      } else {
         /* The "Register Region Restrictions" page says for BDW (and newer,
          * presumably):
          *
          *     "When destination spans two registers, the source may be one or
          *      two registers. The destination elements must be evenly split
          *      between the two registers."
          *
          * Thus we can do a single add(16) in SIMD8 or an add(32) in SIMD16
          * to compute our pixel centers.
          */
         const fs_builder dbld =
            abld.exec_all().group(hbld.dispatch_width() * 2, 0);
         brw_reg int_pixel_xy = dbld.vgrf(BRW_TYPE_UW);

         dbld.ADD(int_pixel_xy,
                  brw_reg(stride(suboffset(gi_uw, 4), 1, 4, 0)),
                  int_pixel_offset_xy);

         hbld.emit(FS_OPCODE_PIXEL_X, offset(s.pixel_x, hbld, i), int_pixel_xy,
                                      horiz_stride(half_int_pixel_offset_x, 0));
         hbld.emit(FS_OPCODE_PIXEL_Y, offset(s.pixel_y, hbld, i), int_pixel_xy,
                                      horiz_stride(half_int_pixel_offset_y, 0));
      }
   }

   abld = bld.annotate("compute pos.z");
   brw_reg coarse_z;
   if (wm_prog_data->coarse_pixel_dispatch != BRW_NEVER &&
       wm_prog_data->uses_depth_w_coefficients) {
      /* In coarse pixel mode, the HW doesn't interpolate Z coordinate
       * properly. In the same way we have to add the coarse pixel size to
       * pixels locations, here we recompute the Z value with 2 coefficients
       * in X & Y axis.
       */
      brw_reg coef_payload = brw_vec8_grf(payload.depth_w_coef_reg, 0);
      const brw_reg x_start = brw_vec1_grf(coef_payload.nr, 2);
      const brw_reg y_start = brw_vec1_grf(coef_payload.nr, 6);
      const brw_reg z_cx    = brw_vec1_grf(coef_payload.nr, 1);
      const brw_reg z_cy    = brw_vec1_grf(coef_payload.nr, 0);
      const brw_reg z_c0    = brw_vec1_grf(coef_payload.nr, 3);

      const brw_reg float_pixel_x = abld.vgrf(BRW_TYPE_F);
      const brw_reg float_pixel_y = abld.vgrf(BRW_TYPE_F);

      abld.ADD(float_pixel_x, s.pixel_x, negate(x_start));
      abld.ADD(float_pixel_y, s.pixel_y, negate(y_start));

      /* r1.0 - 0:7 ActualCoarsePixelShadingSize.X */
      const brw_reg u8_cps_width = brw_reg(retype(brw_vec1_grf(1, 0), BRW_TYPE_UB));
      /* r1.0 - 15:8 ActualCoarsePixelShadingSize.Y */
      const brw_reg u8_cps_height = byte_offset(u8_cps_width, 1);
      const brw_reg u32_cps_width = abld.vgrf(BRW_TYPE_UD);
      const brw_reg u32_cps_height = abld.vgrf(BRW_TYPE_UD);
      abld.MOV(u32_cps_width, u8_cps_width);
      abld.MOV(u32_cps_height, u8_cps_height);

      const brw_reg f_cps_width = abld.vgrf(BRW_TYPE_F);
      const brw_reg f_cps_height = abld.vgrf(BRW_TYPE_F);
      abld.MOV(f_cps_width, u32_cps_width);
      abld.MOV(f_cps_height, u32_cps_height);

      /* Center in the middle of the coarse pixel. */
      abld.MAD(float_pixel_x, float_pixel_x, brw_imm_f(0.5f), f_cps_width);
      abld.MAD(float_pixel_y, float_pixel_y, brw_imm_f(0.5f), f_cps_height);

      coarse_z = abld.vgrf(BRW_TYPE_F);
      abld.MAD(coarse_z, z_c0, z_cx, float_pixel_x);
      abld.MAD(coarse_z, coarse_z, z_cy, float_pixel_y);
   }

   if (wm_prog_data->uses_src_depth)
      s.pixel_z = fetch_payload_reg(bld, payload.source_depth_reg);

   if (wm_prog_data->uses_depth_w_coefficients ||
       wm_prog_data->uses_src_depth) {
      brw_reg sample_z = s.pixel_z;

      switch (wm_prog_data->coarse_pixel_dispatch) {
      case BRW_NEVER:
         break;

      case BRW_SOMETIMES:
         assert(wm_prog_data->uses_src_depth);
         assert(wm_prog_data->uses_depth_w_coefficients);
         s.pixel_z = abld.vgrf(BRW_TYPE_F);

         /* We re-use the check_dynamic_msaa_flag() call from above */
         set_predicate(BRW_PREDICATE_NORMAL,
                       abld.SEL(s.pixel_z, coarse_z, sample_z));
         break;

      case BRW_ALWAYS:
         assert(!wm_prog_data->uses_src_depth);
         assert(wm_prog_data->uses_depth_w_coefficients);
         s.pixel_z = coarse_z;
         break;
      }
   }

   if (wm_prog_data->uses_src_w) {
      abld = bld.annotate("compute pos.w");
      s.pixel_w = fetch_payload_reg(abld, payload.source_w_reg);
      s.wpos_w = bld.vgrf(BRW_TYPE_F);
      abld.emit(SHADER_OPCODE_RCP, s.wpos_w, s.pixel_w);
   }

   if (wm_key->persample_interp == BRW_SOMETIMES) {
      assert(!devinfo->needs_unlit_centroid_workaround);

      const fs_builder ubld = bld.exec_all().group(16, 0);
      bool loaded_flag = false;

      for (int i = 0; i < BRW_BARYCENTRIC_MODE_COUNT; ++i) {
         if (!(wm_prog_data->barycentric_interp_modes & BITFIELD_BIT(i)))
            continue;

         /* The sample mode will always be the top bit set in the perspective
          * or non-perspective section.  In the case where no SAMPLE mode was
          * requested, wm_prog_data_barycentric_modes() will swap out the top
          * mode for SAMPLE so this works regardless of whether SAMPLE was
          * requested or not.
          */
         int sample_mode;
         if (BITFIELD_BIT(i) & BRW_BARYCENTRIC_NONPERSPECTIVE_BITS) {
            sample_mode = util_last_bit(wm_prog_data->barycentric_interp_modes &
                                        BRW_BARYCENTRIC_NONPERSPECTIVE_BITS) - 1;
         } else {
            sample_mode = util_last_bit(wm_prog_data->barycentric_interp_modes &
                                        BRW_BARYCENTRIC_PERSPECTIVE_BITS) - 1;
         }
         assert(wm_prog_data->barycentric_interp_modes &
                BITFIELD_BIT(sample_mode));

         if (i == sample_mode)
            continue;

         uint8_t *barys = payload.barycentric_coord_reg[i];

         uint8_t *sample_barys = payload.barycentric_coord_reg[sample_mode];
         assert(barys[0] && sample_barys[0]);

         if (!loaded_flag) {
            check_dynamic_msaa_flag(ubld, wm_prog_data,
                                    INTEL_MSAA_FLAG_PERSAMPLE_INTERP);
         }

         for (unsigned j = 0; j < s.dispatch_width / 8; j++) {
            set_predicate(
               BRW_PREDICATE_NORMAL,
               ubld.MOV(brw_vec8_grf(barys[j / 2] + (j % 2) * 2, 0),
                        brw_vec8_grf(sample_barys[j / 2] + (j % 2) * 2, 0)));
         }
      }
   }

   for (int i = 0; i < BRW_BARYCENTRIC_MODE_COUNT; ++i) {
      s.delta_xy[i] = fetch_barycentric_reg(
         bld, payload.barycentric_coord_reg[i]);
   }

   uint32_t centroid_modes = wm_prog_data->barycentric_interp_modes &
      (1 << BRW_BARYCENTRIC_PERSPECTIVE_CENTROID |
       1 << BRW_BARYCENTRIC_NONPERSPECTIVE_CENTROID);

   if (devinfo->needs_unlit_centroid_workaround && centroid_modes) {
      /* Get the pixel/sample mask into f0 so that we know which
       * pixels are lit.  Then, for each channel that is unlit,
       * replace the centroid data with non-centroid data.
       */
      for (unsigned i = 0; i < DIV_ROUND_UP(s.dispatch_width, 16); i++) {
         bld.exec_all().group(1, 0)
            .MOV(retype(brw_flag_reg(0, i), BRW_TYPE_UW),
                 retype(brw_vec1_grf(1 + i, 7), BRW_TYPE_UW));
      }

      for (int i = 0; i < BRW_BARYCENTRIC_MODE_COUNT; ++i) {
         if (!(centroid_modes & (1 << i)))
            continue;

         const brw_reg centroid_delta_xy = s.delta_xy[i];
         const brw_reg &pixel_delta_xy = s.delta_xy[i - 1];

         s.delta_xy[i] = bld.vgrf(BRW_TYPE_F, 2);

         for (unsigned c = 0; c < 2; c++) {
            for (unsigned q = 0; q < s.dispatch_width / 8; q++) {
               set_predicate(BRW_PREDICATE_NORMAL,
                  bld.quarter(q).SEL(
                     quarter(offset(s.delta_xy[i], bld, c), q),
                     quarter(offset(centroid_delta_xy, bld, c), q),
                     quarter(offset(pixel_delta_xy, bld, c), q)));
            }
         }
      }
   }
}


/**
 * Once we've generated code, try to convert normal FS_OPCODE_FB_WRITE
 * instructions to FS_OPCODE_REP_FB_WRITE.
 */
static void
brw_emit_repclear_shader(fs_visitor &s)
{
   brw_wm_prog_key *key = (brw_wm_prog_key*) s.key;
   fs_inst *write = NULL;

   assert(s.devinfo->ver < 20);
   assert(s.uniforms == 0);
   assume(key->nr_color_regions > 0);

   brw_reg color_output = retype(brw_vec4_grf(127, 0), BRW_TYPE_UD);
   brw_reg header = retype(brw_vec8_grf(125, 0), BRW_TYPE_UD);

   /* We pass the clear color as a flat input.  Copy it to the output. */
   brw_reg color_input =
      brw_make_reg(FIXED_GRF, 2, 3, 0, 0, BRW_TYPE_UD,
              BRW_VERTICAL_STRIDE_8, BRW_WIDTH_2, BRW_HORIZONTAL_STRIDE_4,
              BRW_SWIZZLE_XYZW, WRITEMASK_XYZW);

   const fs_builder bld = fs_builder(&s).at_end();
   bld.exec_all().group(4, 0).MOV(color_output, color_input);

   if (key->nr_color_regions > 1) {
      /* Copy g0..g1 as the message header */
      bld.exec_all().group(16, 0)
         .MOV(header, retype(brw_vec8_grf(0, 0), BRW_TYPE_UD));
   }

   for (int i = 0; i < key->nr_color_regions; ++i) {
      if (i > 0)
         bld.exec_all().group(1, 0).MOV(component(header, 2), brw_imm_ud(i));

      write = bld.emit(SHADER_OPCODE_SEND);
      write->resize_sources(3);
      write->sfid = GFX6_SFID_DATAPORT_RENDER_CACHE;
      write->src[0] = brw_imm_ud(0);
      write->src[1] = brw_imm_ud(0);
      write->src[2] = i == 0 ? color_output : header;
      write->check_tdr = true;
      write->send_has_side_effects = true;
      write->desc = brw_fb_write_desc(s.devinfo, i,
         BRW_DATAPORT_RENDER_TARGET_WRITE_SIMD16_SINGLE_SOURCE_REPLICATED,
         i == key->nr_color_regions - 1, false);

      /* We can use a headerless message for the first render target */
      write->header_size = i == 0 ? 0 : 2;
      write->mlen = 1 + write->header_size;
   }
   write->eot = true;
   write->last_rt = true;

   brw_calculate_cfg(s);

   s.first_non_payload_grf = s.payload().num_regs;

   brw_fs_lower_scoreboard(s);
}

/**
 * Turn one of the two CENTROID barycentric modes into PIXEL mode.
 */
static enum brw_barycentric_mode
centroid_to_pixel(enum brw_barycentric_mode bary)
{
   assert(bary == BRW_BARYCENTRIC_PERSPECTIVE_CENTROID ||
          bary == BRW_BARYCENTRIC_NONPERSPECTIVE_CENTROID);
   return (enum brw_barycentric_mode) ((unsigned) bary - 1);
}

static void
calculate_urb_setup(const struct intel_device_info *devinfo,
                    const struct brw_wm_prog_key *key,
                    struct brw_wm_prog_data *prog_data,
                    const nir_shader *nir,
                    const struct brw_mue_map *mue_map)
{
   memset(prog_data->urb_setup, -1, sizeof(prog_data->urb_setup));
   memset(prog_data->urb_setup_channel, 0, sizeof(prog_data->urb_setup_channel));

   int urb_next = 0; /* in vec4s */

   const uint64_t inputs_read =
      nir->info.inputs_read & ~nir->info.per_primitive_inputs;

   /* Figure out where each of the incoming setup attributes lands. */
   if (key->mesh_input != BRW_NEVER) {
      /* Per-Primitive Attributes are laid out by Hardware before the regular
       * attributes, so order them like this to make easy later to map setup
       * into real HW registers.
       */
      if (nir->info.per_primitive_inputs) {
         uint64_t per_prim_inputs_read =
               nir->info.inputs_read & nir->info.per_primitive_inputs;

         /* In Mesh, PRIMITIVE_SHADING_RATE, VIEWPORT and LAYER slots
          * are always at the beginning, because they come from MUE
          * Primitive Header, not Per-Primitive Attributes.
          */
         const uint64_t primitive_header_bits = VARYING_BIT_VIEWPORT |
                                                VARYING_BIT_LAYER |
                                                VARYING_BIT_PRIMITIVE_SHADING_RATE;

         if (mue_map) {
            unsigned per_prim_start_dw = mue_map->per_primitive_start_dw;
            unsigned per_prim_size_dw = mue_map->per_primitive_pitch_dw;

            bool reads_header = (per_prim_inputs_read & primitive_header_bits) != 0;

            if (reads_header || mue_map->user_data_in_primitive_header) {
               /* Primitive Shading Rate, Layer and Viewport live in the same
                * 4-dwords slot (psr is dword 0, layer is dword 1, and viewport
                * is dword 2).
                */
               if (per_prim_inputs_read & VARYING_BIT_PRIMITIVE_SHADING_RATE)
                  prog_data->urb_setup[VARYING_SLOT_PRIMITIVE_SHADING_RATE] = 0;

               if (per_prim_inputs_read & VARYING_BIT_LAYER)
                  prog_data->urb_setup[VARYING_SLOT_LAYER] = 0;

               if (per_prim_inputs_read & VARYING_BIT_VIEWPORT)
                  prog_data->urb_setup[VARYING_SLOT_VIEWPORT] = 0;

               per_prim_inputs_read &= ~primitive_header_bits;
            } else {
               /* If fs doesn't need primitive header, then it won't be made
                * available through SBE_MESH, so we have to skip them when
                * calculating offset from start of per-prim data.
                */
               per_prim_start_dw += mue_map->per_primitive_header_size_dw;
               per_prim_size_dw -= mue_map->per_primitive_header_size_dw;
            }

            u_foreach_bit64(i, per_prim_inputs_read) {
               int start = mue_map->start_dw[i];

               assert(start >= 0);
               assert(mue_map->len_dw[i] > 0);

               assert(unsigned(start) >= per_prim_start_dw);
               unsigned pos_dw = unsigned(start) - per_prim_start_dw;

               prog_data->urb_setup[i] = urb_next + pos_dw / 4;
               prog_data->urb_setup_channel[i] = pos_dw % 4;
            }

            urb_next = per_prim_size_dw / 4;
         } else {
            /* With no MUE map, we never read the primitive header, and
             * per-primitive attributes won't be packed either, so just lay
             * them in varying order.
             */
            per_prim_inputs_read &= ~primitive_header_bits;

            for (unsigned i = 0; i < VARYING_SLOT_MAX; i++) {
               if (per_prim_inputs_read & BITFIELD64_BIT(i)) {
                  prog_data->urb_setup[i] = urb_next++;
               }
            }

            /* The actual setup attributes later must be aligned to a full GRF. */
            urb_next = ALIGN(urb_next, 2);
         }

         prog_data->num_per_primitive_inputs = urb_next;
      }

      const uint64_t clip_dist_bits = VARYING_BIT_CLIP_DIST0 |
                                      VARYING_BIT_CLIP_DIST1;

      uint64_t unique_fs_attrs = inputs_read & BRW_FS_VARYING_INPUT_MASK;

      if (inputs_read & clip_dist_bits) {
         assert(!mue_map || mue_map->per_vertex_header_size_dw > 8);
         unique_fs_attrs &= ~clip_dist_bits;
      }

      if (mue_map) {
         unsigned per_vertex_start_dw = mue_map->per_vertex_start_dw;
         unsigned per_vertex_size_dw = mue_map->per_vertex_pitch_dw;

         /* Per-Vertex header is available to fragment shader only if there's
          * user data there.
          */
         if (!mue_map->user_data_in_vertex_header) {
            per_vertex_start_dw += 8;
            per_vertex_size_dw -= 8;
         }

         /* In Mesh, CLIP_DIST slots are always at the beginning, because
          * they come from MUE Vertex Header, not Per-Vertex Attributes.
          */
         if (inputs_read & clip_dist_bits) {
            prog_data->urb_setup[VARYING_SLOT_CLIP_DIST0] = urb_next;
            prog_data->urb_setup[VARYING_SLOT_CLIP_DIST1] = urb_next + 1;
         } else if (mue_map && mue_map->per_vertex_header_size_dw > 8) {
            /* Clip distances are in MUE, but we are not reading them in FS. */
            per_vertex_start_dw += 8;
            per_vertex_size_dw -= 8;
         }

         /* Per-Vertex attributes are laid out ordered.  Because we always link
          * Mesh and Fragment shaders, the which slots are written and read by
          * each of them will match. */
         u_foreach_bit64(i, unique_fs_attrs) {
            int start = mue_map->start_dw[i];

            assert(start >= 0);
            assert(mue_map->len_dw[i] > 0);

            assert(unsigned(start) >= per_vertex_start_dw);
            unsigned pos_dw = unsigned(start) - per_vertex_start_dw;

            prog_data->urb_setup[i] = urb_next + pos_dw / 4;
            prog_data->urb_setup_channel[i] = pos_dw % 4;
         }

         urb_next += per_vertex_size_dw / 4;
      } else {
         /* If we don't have an MUE map, just lay down the inputs the FS reads
          * in varying order, as we do for the legacy pipeline.
          */
         if (inputs_read & clip_dist_bits) {
            prog_data->urb_setup[VARYING_SLOT_CLIP_DIST0] = urb_next++;
            prog_data->urb_setup[VARYING_SLOT_CLIP_DIST1] = urb_next++;
         }

         for (unsigned int i = 0; i < VARYING_SLOT_MAX; i++) {
            if (unique_fs_attrs & BITFIELD64_BIT(i))
               prog_data->urb_setup[i] = urb_next++;
         }
      }
   } else {
      assert(!nir->info.per_primitive_inputs);

      uint64_t vue_header_bits =
         VARYING_BIT_PSIZ | VARYING_BIT_LAYER | VARYING_BIT_VIEWPORT;

      uint64_t unique_fs_attrs = inputs_read & BRW_FS_VARYING_INPUT_MASK;

      /* VUE header fields all live in the same URB slot, so we pass them
       * as a single FS input attribute.  We want to only count them once.
       */
      if (inputs_read & vue_header_bits) {
         unique_fs_attrs &= ~vue_header_bits;
         unique_fs_attrs |= VARYING_BIT_PSIZ;
      }

      if (util_bitcount64(unique_fs_attrs) <= 16) {
         /* The SF/SBE pipeline stage can do arbitrary rearrangement of the
          * first 16 varying inputs, so we can put them wherever we want.
          * Just put them in order.
          *
          * This is useful because it means that (a) inputs not used by the
          * fragment shader won't take up valuable register space, and (b) we
          * won't have to recompile the fragment shader if it gets paired with
          * a different vertex (or geometry) shader.
          *
          * VUE header fields share the same FS input attribute.
          */
         if (inputs_read & vue_header_bits) {
            if (inputs_read & VARYING_BIT_PSIZ)
               prog_data->urb_setup[VARYING_SLOT_PSIZ] = urb_next;
            if (inputs_read & VARYING_BIT_LAYER)
               prog_data->urb_setup[VARYING_SLOT_LAYER] = urb_next;
            if (inputs_read & VARYING_BIT_VIEWPORT)
               prog_data->urb_setup[VARYING_SLOT_VIEWPORT] = urb_next;

            urb_next++;
         }

         for (unsigned int i = 0; i < VARYING_SLOT_MAX; i++) {
            if (inputs_read & BRW_FS_VARYING_INPUT_MASK & ~vue_header_bits &
                BITFIELD64_BIT(i)) {
               prog_data->urb_setup[i] = urb_next++;
            }
         }
      } else {
         /* We have enough input varyings that the SF/SBE pipeline stage can't
          * arbitrarily rearrange them to suit our whim; we have to put them
          * in an order that matches the output of the previous pipeline stage
          * (geometry or vertex shader).
          */

         /* Re-compute the VUE map here in the case that the one coming from
          * geometry has more than one position slot (used for Primitive
          * Replication).
          */
         struct intel_vue_map prev_stage_vue_map;
         brw_compute_vue_map(devinfo, &prev_stage_vue_map,
                             key->input_slots_valid,
                             nir->info.separate_shader, 1);

         int first_slot =
            brw_compute_first_urb_slot_required(inputs_read,
                                                &prev_stage_vue_map);

         assert(prev_stage_vue_map.num_slots <= first_slot + 32);
         for (int slot = first_slot; slot < prev_stage_vue_map.num_slots;
              slot++) {
            int varying = prev_stage_vue_map.slot_to_varying[slot];
            if (varying != BRW_VARYING_SLOT_PAD &&
                (inputs_read & BRW_FS_VARYING_INPUT_MASK &
                 BITFIELD64_BIT(varying))) {
               prog_data->urb_setup[varying] = slot - first_slot;
            }
         }
         urb_next = prev_stage_vue_map.num_slots - first_slot;
      }
   }

   prog_data->num_varying_inputs = urb_next - prog_data->num_per_primitive_inputs;
   prog_data->inputs = inputs_read;

   brw_compute_urb_setup_index(prog_data);
}
static bool
is_used_in_not_interp_frag_coord(nir_def *def)
{
   nir_foreach_use_including_if(src, def) {
      if (nir_src_is_if(src))
         return true;

      if (nir_src_parent_instr(src)->type != nir_instr_type_intrinsic)
         return true;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(nir_src_parent_instr(src));
      if (intrin->intrinsic != nir_intrinsic_load_frag_coord)
         return true;
   }

   return false;
}

/**
 * Return a bitfield where bit n is set if barycentric interpolation mode n
 * (see enum brw_barycentric_mode) is needed by the fragment shader.
 *
 * We examine the load_barycentric intrinsics rather than looking at input
 * variables so that we catch interpolateAtCentroid() messages too, which
 * also need the BRW_BARYCENTRIC_[NON]PERSPECTIVE_CENTROID mode set up.
 */
static unsigned
brw_compute_barycentric_interp_modes(const struct intel_device_info *devinfo,
                                     const struct brw_wm_prog_key *key,
                                     const nir_shader *shader)
{
   unsigned barycentric_interp_modes = 0;

   nir_foreach_function_impl(impl, shader) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            switch (intrin->intrinsic) {
            case nir_intrinsic_load_barycentric_pixel:
            case nir_intrinsic_load_barycentric_centroid:
            case nir_intrinsic_load_barycentric_sample:
            case nir_intrinsic_load_barycentric_at_sample:
            case nir_intrinsic_load_barycentric_at_offset:
               break;
            default:
               continue;
            }

            /* Ignore WPOS; it doesn't require interpolation. */
            if (!is_used_in_not_interp_frag_coord(&intrin->def))
               continue;

            nir_intrinsic_op bary_op = intrin->intrinsic;
            enum brw_barycentric_mode bary =
               brw_barycentric_mode(key, intrin);

            barycentric_interp_modes |= 1 << bary;

            if (devinfo->needs_unlit_centroid_workaround &&
                bary_op == nir_intrinsic_load_barycentric_centroid)
               barycentric_interp_modes |= 1 << centroid_to_pixel(bary);
         }
      }
   }

   return barycentric_interp_modes;
}

/**
 * Return a bitfield where bit n is set if barycentric interpolation
 * mode n (see enum brw_barycentric_mode) is needed by the fragment
 * shader barycentric intrinsics that take an explicit offset or
 * sample as argument.
 */
static unsigned
brw_compute_offset_barycentric_interp_modes(const struct brw_wm_prog_key *key,
                                            const nir_shader *shader)
{
   unsigned barycentric_interp_modes = 0;

   nir_foreach_function_impl(impl, shader) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            if (intrin->intrinsic == nir_intrinsic_load_barycentric_at_offset ||
                intrin->intrinsic == nir_intrinsic_load_barycentric_at_sample)
               barycentric_interp_modes |= 1 << brw_barycentric_mode(key, intrin);
         }
      }
   }

   return barycentric_interp_modes;
}

static void
brw_compute_flat_inputs(struct brw_wm_prog_data *prog_data,
                        const nir_shader *shader)
{
   prog_data->flat_inputs = 0;

   const unsigned per_vertex_start = prog_data->num_per_primitive_inputs;

   nir_foreach_shader_in_variable(var, shader) {
      /* flat shading */
      if (var->data.interpolation != INTERP_MODE_FLAT)
         continue;

      if (var->data.per_primitive)
         continue;

      unsigned slots = glsl_count_attribute_slots(var->type, false);
      for (unsigned s = 0; s < slots; s++) {
         int input_index = prog_data->urb_setup[var->data.location + s] - per_vertex_start;

         if (input_index >= 0)
            prog_data->flat_inputs |= 1 << input_index;
      }
   }
}

static uint8_t
computed_depth_mode(const nir_shader *shader)
{
   if (shader->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_DEPTH)) {
      switch (shader->info.fs.depth_layout) {
      case FRAG_DEPTH_LAYOUT_NONE:
      case FRAG_DEPTH_LAYOUT_ANY:
         return BRW_PSCDEPTH_ON;
      case FRAG_DEPTH_LAYOUT_GREATER:
         return BRW_PSCDEPTH_ON_GE;
      case FRAG_DEPTH_LAYOUT_LESS:
         return BRW_PSCDEPTH_ON_LE;
      case FRAG_DEPTH_LAYOUT_UNCHANGED:
         /* We initially set this to OFF, but having the shader write the
          * depth means we allocate register space in the SEND message. The
          * difference between the SEND register count and the OFF state
          * programming makes the HW hang.
          *
          * Removing the depth writes also leads to test failures. So use
          * LesserThanOrEqual, which fits writing the same value
          * (unchanged/equal).
          *
          */
         return BRW_PSCDEPTH_ON_LE;
      }
   }
   return BRW_PSCDEPTH_OFF;
}

static void
brw_nir_populate_wm_prog_data(nir_shader *shader,
                              const struct intel_device_info *devinfo,
                              const struct brw_wm_prog_key *key,
                              struct brw_wm_prog_data *prog_data,
                              const struct brw_mue_map *mue_map)
{
   prog_data->uses_kill = shader->info.fs.uses_discard;
   prog_data->uses_omask = !key->ignore_sample_mask_out &&
      (shader->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_SAMPLE_MASK));
   prog_data->max_polygons = 1;
   prog_data->computed_depth_mode = computed_depth_mode(shader);
   prog_data->computed_stencil =
      shader->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_STENCIL);

   prog_data->sample_shading =
      shader->info.fs.uses_sample_shading ||
      shader->info.outputs_read;

   assert(key->multisample_fbo != BRW_NEVER ||
          key->persample_interp == BRW_NEVER);

   prog_data->persample_dispatch = key->persample_interp;
   if (prog_data->sample_shading)
      prog_data->persample_dispatch = BRW_ALWAYS;

   /* We can only persample dispatch if we have a multisample FBO */
   prog_data->persample_dispatch = MIN2(prog_data->persample_dispatch,
                                        key->multisample_fbo);

   /* Currently only the Vulkan API allows alpha_to_coverage to be dynamic. If
    * persample_dispatch & multisample_fbo are not dynamic, Anv should be able
    * to definitively tell whether alpha_to_coverage is on or off.
    */
   prog_data->alpha_to_coverage = key->alpha_to_coverage;

   prog_data->uses_sample_mask =
      BITSET_TEST(shader->info.system_values_read, SYSTEM_VALUE_SAMPLE_MASK_IN);

   /* From the Ivy Bridge PRM documentation for 3DSTATE_PS:
    *
    *    "MSDISPMODE_PERSAMPLE is required in order to select
    *    POSOFFSET_SAMPLE"
    *
    * So we can only really get sample positions if we are doing real
    * per-sample dispatch.  If we need gl_SamplePosition and we don't have
    * persample dispatch, we hard-code it to 0.5.
    */
   prog_data->uses_pos_offset =
      prog_data->persample_dispatch != BRW_NEVER &&
      (BITSET_TEST(shader->info.system_values_read,
                   SYSTEM_VALUE_SAMPLE_POS) ||
       BITSET_TEST(shader->info.system_values_read,
                   SYSTEM_VALUE_SAMPLE_POS_OR_CENTER));

   prog_data->early_fragment_tests = shader->info.fs.early_fragment_tests;
   prog_data->post_depth_coverage = shader->info.fs.post_depth_coverage;
   prog_data->inner_coverage = shader->info.fs.inner_coverage;

   prog_data->barycentric_interp_modes =
      brw_compute_barycentric_interp_modes(devinfo, key, shader);

   /* From the BDW PRM documentation for 3DSTATE_WM:
    *
    *    "MSDISPMODE_PERSAMPLE is required in order to select Perspective
    *     Sample or Non- perspective Sample barycentric coordinates."
    *
    * So cleanup any potentially set sample barycentric mode when not in per
    * sample dispatch.
    */
   if (prog_data->persample_dispatch == BRW_NEVER) {
      prog_data->barycentric_interp_modes &=
         ~BITFIELD_BIT(BRW_BARYCENTRIC_PERSPECTIVE_SAMPLE);
   }

   if (devinfo->ver >= 20) {
      const unsigned offset_bary_modes =
         brw_compute_offset_barycentric_interp_modes(key, shader);

      prog_data->uses_npc_bary_coefficients =
         offset_bary_modes & BRW_BARYCENTRIC_NONPERSPECTIVE_BITS;
      prog_data->uses_pc_bary_coefficients =
         offset_bary_modes & ~BRW_BARYCENTRIC_NONPERSPECTIVE_BITS;
      prog_data->uses_sample_offsets =
         offset_bary_modes & ((1 << BRW_BARYCENTRIC_PERSPECTIVE_SAMPLE) |
                              (1 << BRW_BARYCENTRIC_NONPERSPECTIVE_SAMPLE));
   }

   prog_data->uses_nonperspective_interp_modes =
      (prog_data->barycentric_interp_modes & BRW_BARYCENTRIC_NONPERSPECTIVE_BITS) ||
      prog_data->uses_npc_bary_coefficients;

   /* The current VK_EXT_graphics_pipeline_library specification requires
    * coarse to specified at compile time. But per sample interpolation can be
    * dynamic. So we should never be in a situation where coarse &
    * persample_interp are both respectively true & BRW_ALWAYS.
    *
    * Coarse will dynamically turned off when persample_interp is active.
    */
   assert(!key->coarse_pixel || key->persample_interp != BRW_ALWAYS);

   prog_data->coarse_pixel_dispatch =
      brw_sometimes_invert(prog_data->persample_dispatch);
   if (!key->coarse_pixel ||
       prog_data->uses_omask ||
       prog_data->sample_shading ||
       prog_data->uses_sample_mask ||
       (prog_data->computed_depth_mode != BRW_PSCDEPTH_OFF) ||
       prog_data->computed_stencil) {
      prog_data->coarse_pixel_dispatch = BRW_NEVER;
   }

   /* ICL PRMs, Volume 9: Render Engine, Shared Functions Pixel Interpolater,
    * Message Descriptor :
    *
    *    "Message Type. Specifies the type of message being sent when
    *     pixel-rate evaluation is requested :
    *
    *     Format = U2
    *       0: Per Message Offset (eval_snapped with immediate offset)
    *       1: Sample Position Offset (eval_sindex)
    *       2: Centroid Position Offset (eval_centroid)
    *       3: Per Slot Offset (eval_snapped with register offset)
    *
    *     Message Type. Specifies the type of message being sent when
    *     coarse-rate evaluation is requested :
    *
    *     Format = U2
    *       0: Coarse to Pixel Mapping Message (internal message)
    *       1: Reserved
    *       2: Coarse Centroid Position (eval_centroid)
    *       3: Per Slot Coarse Pixel Offset (eval_snapped with register offset)"
    *
    * The Sample Position Offset is marked as reserved for coarse rate
    * evaluation and leads to hangs if we try to use it. So disable coarse
    * pixel shading if we have any intrinsic that will result in a pixel
    * interpolater message at sample.
    */
   if (intel_nir_pulls_at_sample(shader))
      prog_data->coarse_pixel_dispatch = BRW_NEVER;

   /* We choose to always enable VMask prior to XeHP, as it would cause
    * us to lose out on the eliminate_find_live_channel() optimization.
    */
   prog_data->uses_vmask = devinfo->verx10 < 125 ||
                           shader->info.fs.needs_quad_helper_invocations ||
                           shader->info.uses_wide_subgroup_intrinsics ||
                           prog_data->coarse_pixel_dispatch != BRW_NEVER;

   prog_data->uses_src_w =
      BITSET_TEST(shader->info.system_values_read, SYSTEM_VALUE_FRAG_COORD);
   prog_data->uses_src_depth =
      BITSET_TEST(shader->info.system_values_read, SYSTEM_VALUE_FRAG_COORD) &&
      prog_data->coarse_pixel_dispatch != BRW_ALWAYS;
   prog_data->uses_depth_w_coefficients = prog_data->uses_pc_bary_coefficients ||
      (BITSET_TEST(shader->info.system_values_read, SYSTEM_VALUE_FRAG_COORD) &&
       prog_data->coarse_pixel_dispatch != BRW_NEVER);

   calculate_urb_setup(devinfo, key, prog_data, shader, mue_map);
   brw_compute_flat_inputs(prog_data, shader);
}

/* From the SKL PRM, Volume 16, Workarounds:
 *
 *   0877  3D   Pixel Shader Hang possible when pixel shader dispatched with
 *              only header phases (R0-R2)
 *
 *   WA: Enable a non-header phase (e.g. push constant) when dispatch would
 *       have been header only.
 *
 * Instead of enabling push constants one can alternatively enable one of the
 * inputs. Here one simply chooses "layer" which shouldn't impose much
 * overhead.
 */
static void
gfx9_ps_header_only_workaround(struct brw_wm_prog_data *wm_prog_data)
{
   if (wm_prog_data->num_varying_inputs)
      return;

   if (wm_prog_data->base.curb_read_length)
      return;

   wm_prog_data->urb_setup[VARYING_SLOT_LAYER] = 0;
   wm_prog_data->num_varying_inputs = 1;

   brw_compute_urb_setup_index(wm_prog_data);
}

static void
brw_assign_urb_setup(fs_visitor &s)
{
   assert(s.stage == MESA_SHADER_FRAGMENT);

   const struct intel_device_info *devinfo = s.devinfo;
   struct brw_wm_prog_data *prog_data = brw_wm_prog_data(s.prog_data);

   int urb_start = s.payload().num_regs + prog_data->base.curb_read_length;

   /* Offset all the urb_setup[] index by the actual position of the
    * setup regs, now that the location of the constants has been chosen.
    */
   foreach_block_and_inst(block, fs_inst, inst, s.cfg) {
      for (int i = 0; i < inst->sources; i++) {
         if (inst->src[i].file == ATTR) {
            /* ATTR brw_reg::nr in the FS is in units of logical scalar
             * inputs each of which consumes 16B on Gfx4-Gfx12.  In
             * single polygon mode this leads to the following layout
             * of the vertex setup plane parameters in the ATTR
             * register file:
             *
             *  brw_reg::nr   Input   Comp0  Comp1  Comp2  Comp3
             *      0       Attr0.x  a1-a0  a2-a0   N/A    a0
             *      1       Attr0.y  a1-a0  a2-a0   N/A    a0
             *      2       Attr0.z  a1-a0  a2-a0   N/A    a0
             *      3       Attr0.w  a1-a0  a2-a0   N/A    a0
             *      4       Attr1.x  a1-a0  a2-a0   N/A    a0
             *     ...
             *
             * In multipolygon mode that no longer works since
             * different channels may be processing polygons with
             * different plane parameters, so each parameter above is
             * represented as a dispatch_width-wide vector:
             *
             *  brw_reg::nr     brw_reg::offset    Input      Comp0     ...    CompN
             *      0                 0          Attr0.x  a1[0]-a0[0] ... a1[N]-a0[N]
             *      0        4 * dispatch_width  Attr0.x  a2[0]-a0[0] ... a2[N]-a0[N]
             *      0        8 * dispatch_width  Attr0.x     N/A      ...     N/A
             *      0       12 * dispatch_width  Attr0.x    a0[0]     ...    a0[N]
             *      1                 0          Attr0.y  a1[0]-a0[0] ... a1[N]-a0[N]
             *     ...
             *
             * Note that many of the components on a single row above
             * are likely to be replicated multiple times (if, say, a
             * single SIMD thread is only processing 2 different
             * polygons), so plane parameters aren't actually stored
             * in GRF memory with that layout to avoid wasting space.
             * Instead we compose ATTR register regions with a 2D
             * region that walks through the parameters of each
             * polygon with the correct stride, reading the parameter
             * corresponding to each channel directly from the PS
             * thread payload.
             *
             * The latter layout corresponds to a param_width equal to
             * dispatch_width, while the former (scalar parameter)
             * layout has a param_width of 1.
             *
             * Gfx20+ represent plane parameters in a format similar
             * to the above, except the parameters are packed in 12B
             * and ordered like "a0, a1-a0, a2-a0" instead of the
             * above vec4 representation with a missing component.
             */
            const unsigned param_width = (s.max_polygons > 1 ? s.dispatch_width : 1);

            /* Size of a single scalar component of a plane parameter
             * in bytes.
             */
            const unsigned chan_sz = 4;
            struct brw_reg reg;
            assert(s.max_polygons > 0);

            /* Calculate the base register on the thread payload of
             * either the block of vertex setup data or the block of
             * per-primitive constant data depending on whether we're
             * accessing a primitive or vertex input.  Also calculate
             * the index of the input within that block.
             */
            const bool per_prim = inst->src[i].nr < prog_data->num_per_primitive_inputs;
            const unsigned base = urb_start +
               (per_prim ? 0 :
                ALIGN(prog_data->num_per_primitive_inputs / 2,
                      reg_unit(devinfo)) * s.max_polygons);
            const unsigned idx = per_prim ? inst->src[i].nr :
               inst->src[i].nr - prog_data->num_per_primitive_inputs;

            /* Translate the offset within the param_width-wide
             * representation described above into an offset and a
             * grf, which contains the plane parameters for the first
             * polygon processed by the thread.
             */
            if (devinfo->ver >= 20 && !per_prim) {
               /* Gfx20+ is able to pack 5 logical input components
                * per 64B register for vertex setup data.
                */
               const unsigned grf = base + idx / 5 * 2 * s.max_polygons;
               assert(inst->src[i].offset / param_width < 12);
               const unsigned delta = idx % 5 * 12 +
                  inst->src[i].offset / (param_width * chan_sz) * chan_sz +
                  inst->src[i].offset % chan_sz;
               reg = byte_offset(retype(brw_vec8_grf(grf, 0), inst->src[i].type),
                                 delta);
            } else {
               /* Earlier platforms and per-primitive block pack 2 logical
                * input components per 32B register.
                */
               const unsigned grf = base + idx / 2 * s.max_polygons;
               assert(inst->src[i].offset / param_width < REG_SIZE / 2);
               const unsigned delta = (idx % 2) * (REG_SIZE / 2) +
                  inst->src[i].offset / (param_width * chan_sz) * chan_sz +
                  inst->src[i].offset % chan_sz;
               reg = byte_offset(retype(brw_vec8_grf(grf, 0), inst->src[i].type),
                                 delta);
            }

            if (s.max_polygons > 1) {
               assert(devinfo->ver >= 12);
               /* Misaligned channel strides that would lead to
                * cross-channel access in the representation above are
                * disallowed.
                */
               assert(inst->src[i].stride * brw_type_size_bytes(inst->src[i].type) == chan_sz);

               /* Number of channels processing the same polygon. */
               const unsigned poly_width = s.dispatch_width / s.max_polygons;
               assert(s.dispatch_width % s.max_polygons == 0);

               /* Accessing a subset of channels of a parameter vector
                * starting from "chan" is necessary to handle
                * SIMD-lowered instructions though.
                */
               const unsigned chan = inst->src[i].offset %
                  (param_width * chan_sz) / chan_sz;
               assert(chan < s.dispatch_width);
               assert(chan % poly_width == 0);
               const unsigned reg_size = reg_unit(devinfo) * REG_SIZE;
               reg = byte_offset(reg, chan / poly_width * reg_size);

               if (inst->exec_size > poly_width) {
                  /* Accessing the parameters for multiple polygons.
                   * Corresponding parameters for different polygons
                   * are stored a GRF apart on the thread payload, so
                   * use that as vertical stride.
                   */
                  const unsigned vstride = reg_size / brw_type_size_bytes(inst->src[i].type);
                  assert(vstride <= 32);
                  assert(chan % poly_width == 0);
                  reg = stride(reg, vstride, poly_width, 0);
               } else {
                  /* Accessing one parameter for a single polygon --
                   * Translate to a scalar region.
                   */
                  assert(chan % poly_width + inst->exec_size <= poly_width);
                  reg = stride(reg, 0, 1, 0);
               }

            } else {
               const unsigned width = inst->src[i].stride == 0 ?
                  1 : MIN2(inst->exec_size, 8);
               reg = stride(reg, width * inst->src[i].stride,
                            width, inst->src[i].stride);
            }

            reg.abs = inst->src[i].abs;
            reg.negate = inst->src[i].negate;
            inst->src[i] = reg;
         }
      }
   }

   /* Each attribute is 4 setup channels, each of which is half a reg,
    * but they may be replicated multiple times for multipolygon
    * dispatch.
    */
   s.first_non_payload_grf += prog_data->num_varying_inputs * 2 * s.max_polygons;

   /* Unlike regular attributes, per-primitive attributes have all 4 channels
    * in the same slot, so each GRF can store two slots.
    */
   assert(prog_data->num_per_primitive_inputs % 2 == 0);
   s.first_non_payload_grf += prog_data->num_per_primitive_inputs / 2 * s.max_polygons;
}

static bool
run_fs(fs_visitor &s, bool allow_spilling, bool do_rep_send)
{
   const struct intel_device_info *devinfo = s.devinfo;
   struct brw_wm_prog_data *wm_prog_data = brw_wm_prog_data(s.prog_data);
   brw_wm_prog_key *wm_key = (brw_wm_prog_key *) s.key;
   const fs_builder bld = fs_builder(&s).at_end();
   const nir_shader *nir = s.nir;

   assert(s.stage == MESA_SHADER_FRAGMENT);

   s.payload_ = new fs_thread_payload(s, s.source_depth_to_render_target);

   if (nir->info.ray_queries > 0)
      s.limit_dispatch_width(16, "SIMD32 not supported with ray queries.\n");

   if (do_rep_send) {
      assert(s.dispatch_width == 16);
      brw_emit_repclear_shader(s);
   } else {
      if (nir->info.inputs_read > 0 ||
          BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_FRAG_COORD) ||
          (nir->info.outputs_read > 0 && !wm_key->coherent_fb_fetch)) {
         brw_emit_interpolation_setup(s);
      }

      /* We handle discards by keeping track of the still-live pixels in f0.1.
       * Initialize it with the dispatched pixels.
       */
      if (devinfo->ver >= 20 || wm_prog_data->uses_kill) {
         const unsigned lower_width = MIN2(s.dispatch_width, 16);
         for (unsigned i = 0; i < s.dispatch_width / lower_width; i++) {
            /* According to the "PS Thread Payload for Normal
             * Dispatch" pages on the BSpec, the dispatch mask is
             * stored in R0.15/R1.15 on gfx20+ and in R1.7/R2.7 on
             * gfx6+.
             */
            const brw_reg dispatch_mask =
               devinfo->ver >= 20 ? xe2_vec1_grf(i, 15) :
                                    brw_vec1_grf(i + 1, 7);
            bld.exec_all().group(1, 0)
               .MOV(brw_sample_mask_reg(bld.group(lower_width, i)),
                    retype(dispatch_mask, BRW_TYPE_UW));
         }
      }

      if (nir->info.writes_memory)
         wm_prog_data->has_side_effects = true;

      nir_to_brw(&s);

      if (s.failed)
	 return false;

      brw_emit_fb_writes(s);

      brw_calculate_cfg(s);

      brw_fs_optimize(s);

      s.assign_curb_setup();

      if (devinfo->ver == 9)
         gfx9_ps_header_only_workaround(wm_prog_data);

      brw_assign_urb_setup(s);

      brw_fs_lower_3src_null_dest(s);
      brw_fs_workaround_memory_fence_before_eot(s);
      brw_fs_workaround_emit_dummy_mov_instruction(s);

      brw_allocate_registers(s, allow_spilling);

      brw_fs_workaround_source_arf_before_eot(s);
   }

   return !s.failed;
}

const unsigned *
brw_compile_fs(const struct brw_compiler *compiler,
               struct brw_compile_fs_params *params)
{
   struct nir_shader *nir = params->base.nir;
   const struct brw_wm_prog_key *key = params->key;
   struct brw_wm_prog_data *prog_data = params->prog_data;
   bool allow_spilling = params->allow_spilling;
   const bool debug_enabled =
      brw_should_print_shader(nir, params->base.debug_flag ?
                                   params->base.debug_flag : DEBUG_WM);

   prog_data->base.stage = MESA_SHADER_FRAGMENT;
   prog_data->base.ray_queries = nir->info.ray_queries;
   prog_data->base.total_scratch = 0;

   const struct intel_device_info *devinfo = compiler->devinfo;
   const unsigned max_subgroup_size = 32;

   brw_nir_apply_key(nir, compiler, &key->base, max_subgroup_size);
   brw_nir_lower_fs_inputs(nir, devinfo, key);
   brw_nir_lower_fs_outputs(nir);

   /* From the SKL PRM, Volume 7, "Alpha Coverage":
    *  "If Pixel Shader outputs oMask, AlphaToCoverage is disabled in
    *   hardware, regardless of the state setting for this feature."
    */
   if (key->alpha_to_coverage != BRW_NEVER) {
      /* Run constant fold optimization in order to get the correct source
       * offset to determine render target 0 store instruction in
       * emit_alpha_to_coverage pass.
       */
      NIR_PASS(_, nir, nir_opt_constant_folding);
      NIR_PASS(_, nir, brw_nir_lower_alpha_to_coverage, key, prog_data);
   }

   NIR_PASS(_, nir, brw_nir_move_interpolation_to_top);
   brw_postprocess_nir(nir, compiler, debug_enabled,
                       key->base.robust_flags);

   brw_nir_populate_wm_prog_data(nir, compiler->devinfo, key, prog_data,
                                 params->mue_map);

   std::unique_ptr<fs_visitor> v8, v16, v32, vmulti;
   cfg_t *simd8_cfg = NULL, *simd16_cfg = NULL, *simd32_cfg = NULL,
      *multi_cfg = NULL;
   float throughput = 0;
   bool has_spilled = false;

   if (devinfo->ver < 20) {
      v8 = std::make_unique<fs_visitor>(compiler, &params->base, key,
                                        prog_data, nir, 8, 1,
                                        params->base.stats != NULL,
                                        debug_enabled);
      if (!run_fs(*v8, allow_spilling, false /* do_rep_send */)) {
         params->base.error_str = ralloc_strdup(params->base.mem_ctx,
                                                v8->fail_msg);
         return NULL;
      } else if (INTEL_SIMD(FS, 8)) {
         simd8_cfg = v8->cfg;

         assert(v8->payload().num_regs % reg_unit(devinfo) == 0);
         prog_data->base.dispatch_grf_start_reg = v8->payload().num_regs / reg_unit(devinfo);

         const performance &perf = v8->performance_analysis.require();
         throughput = MAX2(throughput, perf.throughput);
         has_spilled = v8->spilled_any_registers;
         allow_spilling = false;
      }
   }

   if (key->coarse_pixel && devinfo->ver < 20) {
      if (prog_data->dual_src_blend) {
         v8->limit_dispatch_width(8, "SIMD16 coarse pixel shading cannot"
                                  " use SIMD8 messages.\n");
      }
      v8->limit_dispatch_width(16, "SIMD32 not supported with coarse"
                               " pixel shading.\n");
   }

   if (!has_spilled &&
       (!v8 || v8->max_dispatch_width >= 16) &&
       (INTEL_SIMD(FS, 16) || params->use_rep_send)) {
      /* Try a SIMD16 compile */
      v16 = std::make_unique<fs_visitor>(compiler, &params->base, key,
                                         prog_data, nir, 16, 1,
                                         params->base.stats != NULL,
                                         debug_enabled);
      if (v8)
         v16->import_uniforms(v8.get());
      if (!run_fs(*v16, allow_spilling, params->use_rep_send)) {
         brw_shader_perf_log(compiler, params->base.log_data,
                             "SIMD16 shader failed to compile: %s\n",
                             v16->fail_msg);
      } else {
         simd16_cfg = v16->cfg;

         assert(v16->payload().num_regs % reg_unit(devinfo) == 0);
         prog_data->dispatch_grf_start_reg_16 = v16->payload().num_regs / reg_unit(devinfo);

         const performance &perf = v16->performance_analysis.require();
         throughput = MAX2(throughput, perf.throughput);
         has_spilled = v16->spilled_any_registers;
         allow_spilling = false;
      }
   }

   const bool simd16_failed = v16 && !simd16_cfg;

   /* Currently, the compiler only supports SIMD32 on SNB+ */
   if (!has_spilled &&
       (!v8 || v8->max_dispatch_width >= 32) &&
       (!v16 || v16->max_dispatch_width >= 32) && !params->use_rep_send &&
       !simd16_failed &&
       INTEL_SIMD(FS, 32)) {
      /* Try a SIMD32 compile */
      v32 = std::make_unique<fs_visitor>(compiler, &params->base, key,
                                         prog_data, nir, 32, 1,
                                         params->base.stats != NULL,
                                         debug_enabled);
      if (v8)
         v32->import_uniforms(v8.get());
      else if (v16)
         v32->import_uniforms(v16.get());

      if (!run_fs(*v32, allow_spilling, false)) {
         brw_shader_perf_log(compiler, params->base.log_data,
                             "SIMD32 shader failed to compile: %s\n",
                             v32->fail_msg);
      } else {
         const performance &perf = v32->performance_analysis.require();

         if (!INTEL_DEBUG(DEBUG_DO32) && throughput >= perf.throughput) {
            brw_shader_perf_log(compiler, params->base.log_data,
                                "SIMD32 shader inefficient\n");
         } else {
            simd32_cfg = v32->cfg;

            assert(v32->payload().num_regs % reg_unit(devinfo) == 0);
            prog_data->dispatch_grf_start_reg_32 = v32->payload().num_regs / reg_unit(devinfo);

            throughput = MAX2(throughput, perf.throughput);
         }
      }
   }

   if (devinfo->ver >= 12 && !has_spilled &&
       params->max_polygons >= 2 && !key->coarse_pixel) {
      fs_visitor *vbase = v8 ? v8.get() : v16 ? v16.get() : v32.get();
      assert(vbase);

      if (devinfo->ver >= 20 &&
          params->max_polygons >= 4 &&
          vbase->max_dispatch_width >= 32 &&
          4 * prog_data->num_varying_inputs <= MAX_VARYING &&
          INTEL_SIMD(FS, 4X8)) {
         /* Try a quad-SIMD8 compile */
         vmulti = std::make_unique<fs_visitor>(compiler, &params->base, key,
                                               prog_data, nir, 32, 4,
                                               params->base.stats != NULL,
                                               debug_enabled);
         vmulti->import_uniforms(vbase);
         if (!run_fs(*vmulti, false, params->use_rep_send)) {
            brw_shader_perf_log(compiler, params->base.log_data,
                                "Quad-SIMD8 shader failed to compile: %s\n",
                                vmulti->fail_msg);
         } else {
            multi_cfg = vmulti->cfg;
            assert(!vmulti->spilled_any_registers);
         }
      }

      if (!multi_cfg && devinfo->ver >= 20 &&
          vbase->max_dispatch_width >= 32 &&
          2 * prog_data->num_varying_inputs <= MAX_VARYING &&
          INTEL_SIMD(FS, 2X16)) {
         /* Try a dual-SIMD16 compile */
         vmulti = std::make_unique<fs_visitor>(compiler, &params->base, key,
                                               prog_data, nir, 32, 2,
                                               params->base.stats != NULL,
                                               debug_enabled);
         vmulti->import_uniforms(vbase);
         if (!run_fs(*vmulti, false, params->use_rep_send)) {
            brw_shader_perf_log(compiler, params->base.log_data,
                                "Dual-SIMD16 shader failed to compile: %s\n",
                                vmulti->fail_msg);
         } else {
            multi_cfg = vmulti->cfg;
            assert(!vmulti->spilled_any_registers);
         }
      }

      if (!multi_cfg && vbase->max_dispatch_width >= 16 &&
          2 * prog_data->num_varying_inputs <= MAX_VARYING &&
          INTEL_SIMD(FS, 2X8)) {
         /* Try a dual-SIMD8 compile */
         vmulti = std::make_unique<fs_visitor>(compiler, &params->base, key,
                                               prog_data, nir, 16, 2,
                                               params->base.stats != NULL,
                                               debug_enabled);
         vmulti->import_uniforms(vbase);
         if (!run_fs(*vmulti, allow_spilling, params->use_rep_send)) {
            brw_shader_perf_log(compiler, params->base.log_data,
                                "Dual-SIMD8 shader failed to compile: %s\n",
                                vmulti->fail_msg);
         } else {
            multi_cfg = vmulti->cfg;
         }
      }

      if (multi_cfg) {
         assert(vmulti->payload().num_regs % reg_unit(devinfo) == 0);
         prog_data->base.dispatch_grf_start_reg = vmulti->payload().num_regs / reg_unit(devinfo);
      }
   }

   /* When the caller requests a repclear shader, they want SIMD16-only */
   if (params->use_rep_send)
      simd8_cfg = NULL;

   fs_generator g(compiler, &params->base, &prog_data->base,
                  MESA_SHADER_FRAGMENT);

   if (unlikely(debug_enabled)) {
      g.enable_debug(ralloc_asprintf(params->base.mem_ctx,
                                     "%s fragment shader %s",
                                     nir->info.label ?
                                        nir->info.label : "unnamed",
                                     nir->info.name));
   }

   struct brw_compile_stats *stats = params->base.stats;
   uint32_t max_dispatch_width = 0;

   if (multi_cfg) {
      prog_data->dispatch_multi = vmulti->dispatch_width;
      prog_data->max_polygons = vmulti->max_polygons;
      g.generate_code(multi_cfg, vmulti->dispatch_width, vmulti->shader_stats,
                      vmulti->performance_analysis.require(),
                      stats, vmulti->max_polygons);
      stats = stats ? stats + 1 : NULL;
      max_dispatch_width = vmulti->dispatch_width;

   } else if (simd8_cfg) {
      prog_data->dispatch_8 = true;
      g.generate_code(simd8_cfg, 8, v8->shader_stats,
                      v8->performance_analysis.require(), stats, 1);
      stats = stats ? stats + 1 : NULL;
      max_dispatch_width = 8;
   }

   if (simd16_cfg) {
      prog_data->dispatch_16 = true;
      prog_data->prog_offset_16 = g.generate_code(
         simd16_cfg, 16, v16->shader_stats,
         v16->performance_analysis.require(), stats, 1);
      stats = stats ? stats + 1 : NULL;
      max_dispatch_width = 16;
   }

   if (simd32_cfg) {
      prog_data->dispatch_32 = true;
      prog_data->prog_offset_32 = g.generate_code(
         simd32_cfg, 32, v32->shader_stats,
         v32->performance_analysis.require(), stats, 1);
      stats = stats ? stats + 1 : NULL;
      max_dispatch_width = 32;
   }

   for (struct brw_compile_stats *s = params->base.stats; s != NULL && s != stats; s++)
      s->max_dispatch_width = max_dispatch_width;

   g.add_const_data(nir->constant_data, nir->constant_data_size);
   return g.get_assembly();
}
