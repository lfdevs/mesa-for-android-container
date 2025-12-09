/*
 * Copyright © 2010 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_eu.h"
#include "brw_shader.h"
#include "brw_analysis.h"
#include "brw_builder.h"
#include "brw_generator.h"
#include "brw_nir.h"
#include "brw_cfg.h"
#include "brw_private.h"
#include "intel_nir.h"
#include "shader_enums.h"
#include "dev/intel_debug.h"
#include "dev/intel_wa.h"

#include <memory>

static brw_fb_write_inst *
brw_emit_single_fb_write(brw_shader &s, const brw_builder &bld,
                         brw_reg color0, brw_reg color1,
                         brw_reg src0_alpha,
                         unsigned target, unsigned components,
                         bool null_rt)
{
   assert(s.stage == MESA_SHADER_FRAGMENT);
   struct brw_wm_prog_data *prog_data = brw_wm_prog_data(s.prog_data);

   brw_reg sources[FB_WRITE_LOGICAL_NUM_SRCS];
   sources[FB_WRITE_LOGICAL_SRC_COLOR0]     = color0;
   sources[FB_WRITE_LOGICAL_SRC_COLOR1]     = color1;
   sources[FB_WRITE_LOGICAL_SRC_SRC0_ALPHA] = src0_alpha;

   if (prog_data->uses_omask)
      sources[FB_WRITE_LOGICAL_SRC_OMASK] = s.sample_mask;
   if (s.nir->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_DEPTH))
      sources[FB_WRITE_LOGICAL_SRC_SRC_DEPTH] = s.frag_depth;
   if (s.nir->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_STENCIL))
      sources[FB_WRITE_LOGICAL_SRC_SRC_STENCIL] = s.frag_stencil;

   brw_fb_write_inst *write = bld.emit(FS_OPCODE_FB_WRITE_LOGICAL, brw_reg(),
                                       sources, ARRAY_SIZE(sources))->as_fb_write();
   write->target     = target;
   write->components = components;
   write->null_rt    = null_rt;
   write->last_rt    = false;

   if (prog_data->uses_kill) {
      write->predicate = BRW_PREDICATE_NORMAL;
      write->flag_subreg = sample_mask_flag_subreg(s);
   }

   return write;
}

static void
brw_do_emit_fb_writes(brw_shader &s, int nr_color_regions, bool replicate_alpha)
{
   struct brw_wm_prog_data *prog_data = brw_wm_prog_data(s.prog_data);
   const brw_builder bld = brw_builder(&s);

   brw_fb_write_inst *write = NULL;
   for (int target = 0; target < nr_color_regions; target++) {
      /* Skip over outputs that weren't written. */
      if (s.outputs[target].file == BAD_FILE)
         continue;

      const brw_builder abld = bld.annotate(
         ralloc_asprintf(s.mem_ctx, "FB write target %d", target));

      brw_reg src0_alpha;
      if (replicate_alpha && target != 0)
         src0_alpha = offset(s.outputs[0], bld, 3);

      write = brw_emit_single_fb_write(s, abld, s.outputs[target],
                                       s.dual_src_output, src0_alpha,
                                       target, 4, false);
   }

   if (write) {
      write->last_rt = true;
      write->eot = true;
   }

   if (write == NULL) {
      struct brw_wm_prog_key *key = (brw_wm_prog_key*) s.key;
      /* Disable null_rt if any non color output is written or if
       * alpha_to_coverage can be enabled. Since the alpha_to_coverage bit is
       * coming from the BLEND_STATE structure and the HW will avoid reading
       * it if null_rt is enabled.
       */
      const bool use_null_rt =
         key->alpha_to_coverage == INTEL_NEVER &&
         !prog_data->uses_omask;

      /* Even if there's no color buffers enabled, we still need to send alpha
       * out the pipeline to our null renderbuffer to support alpha-testing,
       * alpha-to-coverage, and so on.
       */
      /* FINISHME: Factor out this frequently recurring pattern into a
       * helper function.
       */
      const brw_reg srcs[] = { reg_undef, reg_undef,
                               reg_undef, offset(s.outputs[0], bld, 3) };
      const brw_reg tmp = bld.vgrf(BRW_TYPE_UD, 4);
      bld.LOAD_PAYLOAD(tmp, srcs, 4, 0);

      write = brw_emit_single_fb_write(s, bld, tmp, reg_undef, reg_undef,
                                      0, 4, use_null_rt);
      write->last_rt = true;
      write->eot = true;
   }
}

static void
brw_emit_fb_writes(brw_shader &s)
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
brw_emit_interpolation_setup(brw_shader &s)
{
   const struct intel_device_info *devinfo = s.devinfo;
   const brw_builder bld = brw_builder(&s);
   brw_builder abld = bld.annotate("compute pixel centers");
   const struct brw_wm_prog_key *wm_key = (brw_wm_prog_key*) s.key;
   struct brw_wm_prog_data *wm_prog_data = brw_wm_prog_data(s.prog_data);

   brw_reg ub_cps_width, ub_cps_height;
   {
      /* The coarse pixel size is delivered as 2 u8 in r1.0 if coarse pixel
       * dispatch is active. Otherwise we need to build a value ourselves.
       */
      brw_builder ubld = bld.exec_all().scalar_group();
      const brw_reg r1_0 = retype(brw_vec1_reg(FIXED_GRF, 1, 0), BRW_TYPE_UD);

      brw_reg cps_size = ubld.vgrf(BRW_TYPE_UD);
      switch (wm_prog_data->coarse_pixel_dispatch) {
      case INTEL_NEVER:
         ubld.MOV(cps_size, brw_imm_ud(0x00000101));
         break;
      case INTEL_SOMETIMES:
         brw_check_dynamic_msaa_flag(ubld, wm_prog_data,
                                     INTEL_MSAA_FLAG_COARSE_RT_WRITES);

         set_predicate_inv(BRW_PREDICATE_NORMAL, false,
                           ubld.MOV(cps_size, r1_0));
         set_predicate_inv(BRW_PREDICATE_NORMAL, true,
                           ubld.MOV(cps_size, brw_imm_ud(0x00000101)));
         break;
      case INTEL_ALWAYS:
         ubld.MOV(cps_size, r1_0);
         break;
      }

      cps_size = component(cps_size, 0);
      ub_cps_width = retype(cps_size, BRW_TYPE_UB);
      ub_cps_height = byte_offset(retype(cps_size, BRW_TYPE_UB), 1);
   }

   s.uw_pixel_x = abld.vgrf(BRW_TYPE_UW);
   s.uw_pixel_y = abld.vgrf(BRW_TYPE_UW);

   brw_fs_thread_payload &payload = s.fs_payload();

   brw_reg int_sample_offset_x, int_sample_offset_y; /* Used on Gen12HP+ */
   brw_reg int_sample_offset_xy; /* Used on Gen8+ */
   brw_reg half_int_sample_offset_x, half_int_sample_offset_y;
   if (wm_prog_data->coarse_pixel_dispatch != INTEL_ALWAYS) {
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
   if (wm_prog_data->coarse_pixel_dispatch != INTEL_NEVER) {
      /* In coarse pixel dispatch we have to do the same ADD instruction that
       * we do in normal per pixel dispatch, except this time we're not adding
       * 1 in each direction, but instead the coarse pixel size.
       */
      const brw_builder dbld =
         abld.exec_all().group(MIN2(16, s.dispatch_width) * 2, 0);

      if (devinfo->verx10 >= 125) {
         /* To build the array of half bytes we do and AND operation with the
          * right mask in X.
          */
         int_coarse_offset_x = dbld.vgrf(BRW_TYPE_UW);
         dbld.AND(int_coarse_offset_x, ub_cps_width, brw_imm_v(0x0f000f00));

         /* And the right mask in Y. */
         int_coarse_offset_y = dbld.vgrf(BRW_TYPE_UW);
         dbld.AND(int_coarse_offset_y, ub_cps_height, brw_imm_v(0x0f0f0000));
      } else {
         /* To build the array of half bytes we do and AND operation with the
          * right mask in X.
          */
         int_coarse_offset_x = dbld.vgrf(BRW_TYPE_UW);
         dbld.AND(int_coarse_offset_x, ub_cps_width, brw_imm_v(0x0000f0f0));

         /* And the right mask in Y. */
         int_coarse_offset_y = dbld.vgrf(BRW_TYPE_UW);
         dbld.AND(int_coarse_offset_y, ub_cps_height, brw_imm_v(0xff000000));

         /* Finally OR the 2 registers. */
         int_coarse_offset_xy = dbld.vgrf(BRW_TYPE_UW);
         dbld.OR(int_coarse_offset_xy, int_coarse_offset_x, int_coarse_offset_y);
      }

      /* Also compute the half coarse size used to center coarses. */
      half_int_coarse_offset_x = bld.vgrf(BRW_TYPE_UW);
      half_int_coarse_offset_y = bld.vgrf(BRW_TYPE_UW);

      bld.SHR(half_int_coarse_offset_x, ub_cps_width, brw_imm_ud(1));
      bld.SHR(half_int_coarse_offset_y, ub_cps_height, brw_imm_ud(1));
   }

   brw_reg int_pixel_offset_x, int_pixel_offset_y; /* Used on Gen12HP+ */
   brw_reg int_pixel_offset_xy; /* Used on Gen8+ */
   brw_reg half_int_pixel_offset_x, half_int_pixel_offset_y;
   switch (wm_prog_data->coarse_pixel_dispatch) {
   case INTEL_NEVER:
      int_pixel_offset_x = int_sample_offset_x;
      int_pixel_offset_y = int_sample_offset_y;
      int_pixel_offset_xy = int_sample_offset_xy;
      half_int_pixel_offset_x = half_int_sample_offset_x;
      half_int_pixel_offset_y = half_int_sample_offset_y;
      break;

   case INTEL_SOMETIMES: {
      const brw_builder dbld =
         abld.exec_all().group(MIN2(16, s.dispatch_width) * 2, 0);

      brw_check_dynamic_msaa_flag(dbld, wm_prog_data,
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

   case INTEL_ALWAYS:
      int_pixel_offset_x = int_coarse_offset_x;
      int_pixel_offset_y = int_coarse_offset_y;
      int_pixel_offset_xy = int_coarse_offset_xy;
      half_int_pixel_offset_x = half_int_coarse_offset_x;
      half_int_pixel_offset_y = half_int_coarse_offset_y;
      break;
   }

   for (unsigned i = 0; i < DIV_ROUND_UP(s.dispatch_width, 16); i++) {
      const brw_builder hbld = abld.group(MIN2(16, s.dispatch_width), i);
      /* According to the "PS Thread Payload for Normal Dispatch"
       * pages on the BSpec, subspan X/Y coordinates are stored in
       * R1.2-R1.5/R2.2-R2.5 on gfx6+, and on R0.10-R0.13/R1.10-R1.13
       * on gfx20+.  gi_reg is the 32B section of the GRF that
       * contains the subspan coordinates.
       */
      const struct brw_reg gi_reg = devinfo->ver >= 20 ? xe2_vec1_grf(i, 8) :
                                    brw_vec1_grf(i + 1, 0);
      const struct brw_reg gi_uw = retype(gi_reg, BRW_TYPE_UW);

      brw_reg int_pixel_x = offset(s.uw_pixel_x, hbld, i);
      brw_reg int_pixel_y = offset(s.uw_pixel_y, hbld, i);

      if (devinfo->verx10 >= 125) {
         /* We compute two sets of int pixel x/y: one with a 2 byte stride for
          * future load_pixel_coord, and one with a 4 byte stride to meet
          * regioning restrictions for the add into a float result that
          * implements the current load_frag_coord.
          */
         const brw_builder dbld =
            abld.exec_all().group(hbld.dispatch_width() * 2, 0);
         const brw_reg int_pixel_x_4b = dbld.vgrf(BRW_TYPE_UW);
         const brw_reg int_pixel_y_4b = dbld.vgrf(BRW_TYPE_UW);

         dbld.ADD(int_pixel_x_4b,
                  brw_reg(stride(suboffset(gi_uw, 4), 2, 8, 0)),
                  int_pixel_offset_x);
         dbld.ADD(int_pixel_y_4b,
                  brw_reg(stride(suboffset(gi_uw, 5), 2, 8, 0)),
                  int_pixel_offset_y);

         if (wm_prog_data->coarse_pixel_dispatch != INTEL_NEVER) {
            brw_inst *addx = dbld.ADD(int_pixel_x_4b, int_pixel_x_4b,
                                      horiz_stride(half_int_pixel_offset_x, 0));
            brw_inst *addy = dbld.ADD(int_pixel_y_4b, int_pixel_y_4b,
                                      horiz_stride(half_int_pixel_offset_y, 0));
            if (wm_prog_data->coarse_pixel_dispatch != INTEL_ALWAYS) {
               addx->predicate = BRW_PREDICATE_NORMAL;
               addy->predicate = BRW_PREDICATE_NORMAL;
            }
         }

         hbld.MOV(int_pixel_x, horiz_stride(int_pixel_x_4b, 2));
         hbld.MOV(int_pixel_y, horiz_stride(int_pixel_y_4b, 2));
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
         const brw_builder dbld =
            abld.exec_all().group(hbld.dispatch_width() * 2, 0);
         brw_reg int_pixel_xy = dbld.vgrf(BRW_TYPE_UW);

         dbld.ADD(int_pixel_xy,
                  brw_reg(stride(suboffset(gi_uw, 4), 1, 4, 0)),
                  int_pixel_offset_xy);

         hbld.emit(FS_OPCODE_PIXEL_X, int_pixel_x, int_pixel_xy,
                                      horiz_stride(half_int_pixel_offset_x, 0));
         hbld.emit(FS_OPCODE_PIXEL_Y, int_pixel_y, int_pixel_xy,
                                      horiz_stride(half_int_pixel_offset_y, 0));
      }
   }

   abld = bld.annotate("compute pos.z");
   brw_reg coarse_z;
   if (wm_prog_data->coarse_pixel_dispatch != INTEL_NEVER &&
       wm_prog_data->uses_depth_w_coefficients) {
      /* In coarse pixel mode, the HW doesn't interpolate Z coordinate
       * properly. In the same way we have to add the coarse pixel size to
       * pixels locations, here we recompute the Z value with 2 coefficients
       * in X & Y axis.
       *
       * src_z = (x - xstart)*z_cx + (y - ystart)*z_cy + z_c0
       */
      brw_reg coef_payload = brw_vec8_grf(payload.depth_w_coef_reg, 0);
      const brw_reg x_start = devinfo->ver >= 20 ?
         brw_vec1_grf(coef_payload.nr, 6) :
         brw_vec1_grf(coef_payload.nr, 2);
      const brw_reg y_start = devinfo->ver >= 20 ?
         brw_vec1_grf(coef_payload.nr, 7) :
         brw_vec1_grf(coef_payload.nr, 6);
      const brw_reg z_cx    = devinfo->ver >= 20 ?
         brw_vec1_grf(coef_payload.nr + 1, 1) :
         brw_vec1_grf(coef_payload.nr, 1);
      const brw_reg z_cy    = devinfo->ver >= 20 ?
         brw_vec1_grf(coef_payload.nr + 1, 0) :
         brw_vec1_grf(coef_payload.nr, 0);
      const brw_reg z_c0    = devinfo->ver >= 20 ?
         brw_vec1_grf(coef_payload.nr + 1, 2) :
         brw_vec1_grf(coef_payload.nr, 3);

      const brw_reg float_pixel_x = abld.vgrf(BRW_TYPE_F);
      const brw_reg float_pixel_y = abld.vgrf(BRW_TYPE_F);

      abld.MOV(float_pixel_x, s.uw_pixel_x);
      abld.MOV(float_pixel_y, s.uw_pixel_y);

      abld.ADD(float_pixel_x, float_pixel_x, negate(x_start));
      abld.ADD(float_pixel_y, float_pixel_y, negate(y_start));

      const brw_reg f_cps_width = abld.vgrf(BRW_TYPE_F);
      const brw_reg f_cps_height = abld.vgrf(BRW_TYPE_F);
      abld.MOV(f_cps_width, ub_cps_width);
      abld.MOV(f_cps_height, ub_cps_height);

      /* Center in the middle of the coarse pixel. */
      abld.MAD(float_pixel_x, float_pixel_x, f_cps_width, brw_imm_f(0.5f));
      abld.MAD(float_pixel_y, float_pixel_y, f_cps_height, brw_imm_f(0.5f));

      coarse_z = abld.vgrf(BRW_TYPE_F);
      abld.MAD(coarse_z, z_c0, z_cx, float_pixel_x);
      abld.MAD(coarse_z, coarse_z, z_cy, float_pixel_y);
   }

   if (wm_prog_data->uses_src_depth)
      s.pixel_z = brw_fetch_payload_reg(bld, payload.source_depth_reg);

   if (wm_prog_data->uses_depth_w_coefficients ||
       wm_prog_data->uses_src_depth) {
      switch (wm_prog_data->coarse_pixel_dispatch) {
      case INTEL_NEVER:
         break;

      case INTEL_SOMETIMES:
         /* We cannot enable 3DSTATE_PS_EXTRA::PixelShaderUsesSourceDepth when
          * coarse is enabled. Here we don't know if it's going to be, but
          * setting brw_wm_prog_data::uses_src_depth dynamically would disturb
          * the payload. So instead rely on the computed coarse_z which will
          * produce a correct value even when coarse is disabled.
          */

         /* Fallthrough */
      case INTEL_ALWAYS:
         assert(!wm_prog_data->uses_src_depth);
         assert(wm_prog_data->uses_depth_w_coefficients);
         s.pixel_z = coarse_z;
         break;
      }
   }

   if (wm_prog_data->uses_src_w) {
      abld = bld.annotate("compute pos.w");
      s.pixel_w = brw_fetch_payload_reg(abld, payload.source_w_reg);
      s.wpos_w = bld.vgrf(BRW_TYPE_F);
      abld.emit(SHADER_OPCODE_RCP, s.wpos_w, s.pixel_w);
   }

   if (wm_key->persample_interp == INTEL_SOMETIMES) {
      const brw_builder ubld = bld.exec_all().group(16, 0);
      bool loaded_flag = false;

      for (int i = 0; i < INTEL_BARYCENTRIC_MODE_COUNT; ++i) {
         if (!(wm_prog_data->barycentric_interp_modes & BITFIELD_BIT(i)))
            continue;

         /* The sample mode will always be the top bit set in the perspective
          * or non-perspective section.  In the case where no SAMPLE mode was
          * requested, wm_prog_data_barycentric_modes() will swap out the top
          * mode for SAMPLE so this works regardless of whether SAMPLE was
          * requested or not.
          */
         int sample_mode;
         if (BITFIELD_BIT(i) & INTEL_BARYCENTRIC_NONPERSPECTIVE_BITS) {
            sample_mode = util_last_bit(wm_prog_data->barycentric_interp_modes &
                                        INTEL_BARYCENTRIC_NONPERSPECTIVE_BITS) - 1;
         } else {
            sample_mode = util_last_bit(wm_prog_data->barycentric_interp_modes &
                                        INTEL_BARYCENTRIC_PERSPECTIVE_BITS) - 1;
         }
         assert(wm_prog_data->barycentric_interp_modes &
                BITFIELD_BIT(sample_mode));

         if (i == sample_mode)
            continue;

         uint8_t *barys = payload.barycentric_coord_reg[i];

         uint8_t *sample_barys = payload.barycentric_coord_reg[sample_mode];
         assert(barys[0] && sample_barys[0]);

         if (!loaded_flag) {
            brw_check_dynamic_msaa_flag(ubld, wm_prog_data,
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

   for (int i = 0; i < INTEL_BARYCENTRIC_MODE_COUNT; ++i) {
      s.delta_xy[i] = brw_fetch_barycentric_reg(
         bld, payload.barycentric_coord_reg[i]);
   }
}


/**
 * Once we've generated code, try to convert normal FS_OPCODE_FB_WRITE
 * instructions to FS_OPCODE_REP_FB_WRITE.
 */
static void
brw_emit_repclear_shader(brw_shader &s)
{
   brw_wm_prog_key *key = (brw_wm_prog_key*) s.key;
   brw_send_inst *write = NULL;

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

   const brw_builder bld = brw_builder(&s);
   bld.exec_all().group(4, 0).MOV(color_output, color_input);

   if (key->nr_color_regions > 1) {
      /* Copy g0..g1 as the message header */
      bld.exec_all().group(16, 0)
         .MOV(header, retype(brw_vec8_grf(0, 0), BRW_TYPE_UD));
   }

   for (int i = 0; i < key->nr_color_regions; ++i) {
      if (i > 0)
         bld.uniform().MOV(component(header, 2), brw_imm_ud(i));

      write = bld.SEND();

      /* We can use a headerless message for the first render target */
      write->header_size = i == 0 ? 0 : 2;
      write->mlen = 1 + write->header_size;

      write->sfid = BRW_SFID_RENDER_CACHE;
      write->src[SEND_SRC_DESC] = brw_imm_ud(
         brw_fb_write_desc(
            s.devinfo, i,
            BRW_DATAPORT_RENDER_TARGET_WRITE_SIMD16_SINGLE_SOURCE_REPLICATED,
            i == key->nr_color_regions - 1, false) |
         brw_message_desc(s.devinfo, write->mlen,
                          0 /* rlen */, write->header_size));
      write->src[SEND_SRC_EX_DESC] = brw_imm_ud(0);
      write->src[SEND_SRC_PAYLOAD1] = i == 0 ? color_output : header;
      write->src[SEND_SRC_PAYLOAD2] = brw_reg();
      write->check_tdr = true;
      write->has_side_effects = true;

      /* We can use a headerless message for the first render target */
      write->header_size = i == 0 ? 0 : 2;
      write->mlen = 1 + write->header_size;
   }
   write->eot = true;

   brw_calculate_cfg(s);

   s.first_non_payload_grf = s.payload().num_regs;

   brw_lower_scoreboard(s);
}

static void
calculate_urb_setup(const struct intel_device_info *devinfo,
                    const struct brw_wm_prog_key *key,
                    struct brw_wm_prog_data *prog_data,
                    nir_shader *nir,
                    const struct brw_mue_map *mue_map,
                    int *per_primitive_offsets)
{
   memset(prog_data->urb_setup, -1, sizeof(prog_data->urb_setup));
   memset(prog_data->urb_setup_channel, 0, sizeof(prog_data->urb_setup_channel));

   int urb_next = 0; /* in vec4s */

   /* Figure out where the PrimitiveID lives, either in the per-vertex block
    * or in the per-primitive block or both.
    */
   const uint64_t per_vert_primitive_id =
      key->mesh_input == INTEL_ALWAYS ? 0 : VARYING_BIT_PRIMITIVE_ID;
   const uint64_t per_prim_primitive_id =
      key->mesh_input == INTEL_NEVER ? 0 : VARYING_BIT_PRIMITIVE_ID;
   const uint64_t inputs_read =
      nir->info.inputs_read &
      (~nir->info.per_primitive_inputs | per_vert_primitive_id);
   const uint64_t per_primitive_header_bits =
      VARYING_BIT_PRIMITIVE_SHADING_RATE |
      VARYING_BIT_LAYER |
      VARYING_BIT_VIEWPORT |
      VARYING_BIT_CULL_PRIMITIVE;
   const uint64_t per_primitive_inputs =
      nir->info.inputs_read &
      (nir->info.per_primitive_inputs | per_prim_primitive_id) &
      ~per_primitive_header_bits;
   uint64_t unique_fs_attrs =
      inputs_read & BRW_FS_VARYING_INPUT_MASK;
   struct intel_vue_map vue_map;
   uint32_t per_primitive_stride = 0, first_read_offset = UINT32_MAX;

   if (mue_map != NULL) {
      memcpy(&vue_map, &mue_map->vue_map, sizeof(vue_map));
      memcpy(per_primitive_offsets,
             mue_map->per_primitive_offsets,
             sizeof(mue_map->per_primitive_offsets));

      if (!mue_map->wa_18019110168_active) {
         u_foreach_bit64(location, per_primitive_inputs) {
            assert(per_primitive_offsets[location] != -1);

            first_read_offset = MIN2(first_read_offset,
                                     (uint32_t)per_primitive_offsets[location]);
            per_primitive_stride =
               MAX2((uint32_t)per_primitive_offsets[location] + 16,
                    per_primitive_stride);
         }
      } else {
         first_read_offset = per_primitive_stride = 0;
      }
   } else {
      brw_compute_vue_map(devinfo, &vue_map, inputs_read,
                          key->base.vue_layout,
                          1 /* pos_slots, TODO */);
      brw_compute_per_primitive_map(per_primitive_offsets,
                                    &per_primitive_stride,
                                    &first_read_offset,
                                    0, nir, nir_var_shader_in,
                                    per_primitive_inputs,
                                    true /* separate_shader */);
   }

   if (per_primitive_stride > first_read_offset) {
      first_read_offset = ROUND_DOWN_TO(first_read_offset, 32);

      /* Remove the first few unused registers */
      for (uint32_t i = 0; i < VARYING_SLOT_MAX; i++) {
         if (per_primitive_offsets[i] == -1)
            continue;
         per_primitive_offsets[i] -= first_read_offset;
      }

      prog_data->num_per_primitive_inputs =
         2 * DIV_ROUND_UP(per_primitive_stride - first_read_offset, 32);
   } else {
      prog_data->num_per_primitive_inputs = 0;
   }

   /* Now do the per-vertex stuff (what used to be legacy pipeline) */
   const uint64_t vue_header_bits = BRW_VUE_HEADER_VARYING_MASK;

   unique_fs_attrs &= ~vue_header_bits;

   /* If Mesh is involved, we cannot do any packing. Documentation doesn't say
    * anything about this but 3DSTATE_SBE_SWIZ does not appear to work when
    * using Mesh.
    */
   if (util_bitcount64(unique_fs_attrs) <= 16 && key->mesh_input == INTEL_NEVER) {
      /* When not in Mesh pipeline mode, the SF/SBE pipeline stage can do
       * arbitrary rearrangement of the first 16 varying inputs, so we can put
       * them wherever we want. Just put them in order.
       *
       * This is useful because it means that (a) inputs not used by the
       * fragment shader won't take up valuable register space, and (b) we
       * won't have to recompile the fragment shader if it gets paired with a
       * different vertex (or geometry) shader.
       */
      for (unsigned int i = 0; i < VARYING_SLOT_MAX; i++) {
         if (inputs_read & BRW_FS_VARYING_INPUT_MASK & ~vue_header_bits &
             BITFIELD64_BIT(i)) {
            prog_data->urb_setup[i] = urb_next++;
         }
      }
   } else {
      /* We have enough input varyings that the SF/SBE pipeline stage can't
       * arbitrarily rearrange them to suit our whim; we have to put them in
       * an order that matches the output of the previous pipeline stage
       * (geometry or vertex shader).
       */
      int first_slot = 0;
      for (int i = 0; i < vue_map.num_slots; i++) {
         int varying = vue_map.slot_to_varying[i];
         if (varying != BRW_VARYING_SLOT_PAD && varying > 0 &&
             (inputs_read & BITFIELD64_BIT(varying)) != 0) {
            first_slot = ROUND_DOWN_TO(i, 2);
            break;
         }
      }

      for (int slot = first_slot; slot < vue_map.num_slots; slot++) {
         int varying = vue_map.slot_to_varying[slot];
         if (varying != BRW_VARYING_SLOT_PAD &&
             (inputs_read & BRW_FS_VARYING_INPUT_MASK &
              BITFIELD64_BIT(varying))) {
            prog_data->urb_setup[varying] = slot - first_slot;
         }
      }
      urb_next = vue_map.num_slots - first_slot;
   }

   prog_data->num_varying_inputs = urb_next;
   prog_data->inputs = inputs_read;
   prog_data->per_primitive_inputs = per_primitive_inputs;

   brw_compute_urb_setup_index(prog_data);
}

/**
 * Return a bitfield where bit n is set if barycentric interpolation mode n
 * (see enum intel_barycentric_mode) is needed by the fragment shader.
 *
 * We examine the load_barycentric intrinsics rather than looking at input
 * variables so that we catch interpolateAtCentroid() messages too, which
 * also need the INTEL_BARYCENTRIC_[NON]PERSPECTIVE_CENTROID mode set up.
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

            enum intel_barycentric_mode bary =
               brw_barycentric_mode(key, intrin);

            barycentric_interp_modes |= 1 << bary;
         }
      }
   }

   return barycentric_interp_modes;
}

/**
 * Return a bitfield where bit n is set if barycentric interpolation
 * mode n (see enum intel_barycentric_mode) is needed by the fragment
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

   nir_foreach_shader_in_variable(var, shader) {
      /* flat shading */
      if (var->data.interpolation != INTERP_MODE_FLAT)
         continue;

      if (var->data.per_primitive)
         continue;

      unsigned slots = glsl_count_attribute_slots(var->type, false);
      for (unsigned s = 0; s < slots; s++) {
         int input_index = prog_data->urb_setup[var->data.location + s];

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
                              const struct brw_mue_map *mue_map,
                              int *per_primitive_offsets)
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
   prog_data->api_sample_shading = key->api_sample_shading;
   prog_data->min_sample_shading = key->min_sample_shading;

   assert(key->multisample_fbo != INTEL_NEVER ||
          key->persample_interp == INTEL_NEVER);

   prog_data->persample_dispatch = key->persample_interp;
   if (prog_data->sample_shading)
      prog_data->persample_dispatch = INTEL_ALWAYS;

   /* We can only persample dispatch if we have a multisample FBO */
   prog_data->persample_dispatch = MIN2(prog_data->persample_dispatch,
                                        key->multisample_fbo);

   /* Currently only the Vulkan API allows alpha_to_coverage to be dynamic. If
    * persample_dispatch & multisample_fbo are not dynamic, Anv should be able
    * to definitively tell whether alpha_to_coverage is on or off.
    */
   prog_data->alpha_to_coverage = key->alpha_to_coverage;

   assert(devinfo->verx10 >= 125 || key->mesh_input == INTEL_NEVER);
   prog_data->mesh_input = key->mesh_input;

   assert(devinfo->verx10 >= 200 || key->provoking_vertex_last == INTEL_NEVER);
   prog_data->provoking_vertex_last = key->provoking_vertex_last;

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
      prog_data->persample_dispatch != INTEL_NEVER &&
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
   if (prog_data->persample_dispatch == INTEL_NEVER) {
      prog_data->barycentric_interp_modes &=
         ~BITFIELD_BIT(INTEL_BARYCENTRIC_PERSPECTIVE_SAMPLE);
   }

   if (devinfo->ver >= 20) {
      const unsigned offset_bary_modes =
         brw_compute_offset_barycentric_interp_modes(key, shader);

      prog_data->vertex_attributes_bypass = brw_needs_vertex_attributes_bypass(shader);

      prog_data->uses_npc_bary_coefficients =
         offset_bary_modes & INTEL_BARYCENTRIC_NONPERSPECTIVE_BITS;
      prog_data->uses_pc_bary_coefficients =
         offset_bary_modes & ~INTEL_BARYCENTRIC_NONPERSPECTIVE_BITS;
      prog_data->uses_sample_offsets =
         offset_bary_modes & ((1 << INTEL_BARYCENTRIC_PERSPECTIVE_SAMPLE) |
                              (1 << INTEL_BARYCENTRIC_NONPERSPECTIVE_SAMPLE));
   }

   prog_data->uses_nonperspective_interp_modes =
      (prog_data->barycentric_interp_modes & INTEL_BARYCENTRIC_NONPERSPECTIVE_BITS) ||
      prog_data->uses_npc_bary_coefficients;

   /* The current VK_EXT_graphics_pipeline_library specification requires
    * coarse to specified at compile time. But per sample interpolation can be
    * dynamic. So we should never be in a situation where coarse &
    * persample_interp are both respectively true & INTEL_ALWAYS.
    *
    * Coarse will dynamically turned off when persample_interp is active.
    */
   assert(!key->coarse_pixel || key->persample_interp != INTEL_ALWAYS);

   prog_data->coarse_pixel_dispatch =
      intel_sometimes_invert(prog_data->persample_dispatch);
   if (!key->coarse_pixel ||
       prog_data->uses_omask ||
       prog_data->sample_shading ||
       prog_data->uses_sample_mask ||
       (prog_data->computed_depth_mode != BRW_PSCDEPTH_OFF) ||
       prog_data->computed_stencil ||
       devinfo->ver < 11) {
      prog_data->coarse_pixel_dispatch = INTEL_NEVER;
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
      prog_data->coarse_pixel_dispatch = INTEL_NEVER;

   /* We choose to always enable VMask prior to XeHP, as it would cause
    * us to lose out on the eliminate_find_live_channel() optimization.
    */
   prog_data->uses_vmask = devinfo->verx10 < 125 ||
                           shader->info.fs.needs_coarse_quad_helper_invocations ||
                           shader->info.uses_wide_subgroup_intrinsics ||
                           prog_data->coarse_pixel_dispatch != INTEL_NEVER;

   prog_data->uses_src_w =
      BITSET_TEST(shader->info.system_values_read, SYSTEM_VALUE_FRAG_COORD_W);
   prog_data->uses_src_depth =
      BITSET_TEST(shader->info.system_values_read, SYSTEM_VALUE_FRAG_COORD_Z) &&
      prog_data->coarse_pixel_dispatch == INTEL_NEVER;
   prog_data->uses_depth_w_coefficients = prog_data->uses_pc_bary_coefficients ||
      (BITSET_TEST(shader->info.system_values_read, SYSTEM_VALUE_FRAG_COORD_Z) &&
       prog_data->coarse_pixel_dispatch != INTEL_NEVER);

   calculate_urb_setup(devinfo, key, prog_data, shader, mue_map, per_primitive_offsets);
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

static brw_reg
remap_attr_reg(brw_shader &s,
               struct brw_wm_prog_data *prog_data,
               const brw_reg &src,
               unsigned urb_start,
               unsigned exec_size)
{
   /* ATTR brw_reg::nr in the FS is in units of logical scalar inputs each of
    * which consumes 16B on Gfx4-Gfx12. In single polygon mode this leads to
    * the following layout of the vertex setup plane parameters in the ATTR
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
    * In multipolygon mode that no longer works since different channels may
    * be processing polygons with different plane parameters, so each
    * parameter above is represented as a dispatch_width-wide vector:
    *
    *  brw_reg::nr     brw_reg::offset    Input      Comp0     ...    CompN
    *      0                 0          Attr0.x  a1[0]-a0[0] ... a1[N]-a0[N]
    *      0        4 * dispatch_width  Attr0.x  a2[0]-a0[0] ... a2[N]-a0[N]
    *      0        8 * dispatch_width  Attr0.x     N/A      ...     N/A
    *      0       12 * dispatch_width  Attr0.x    a0[0]     ...    a0[N]
    *      1                 0          Attr0.y  a1[0]-a0[0] ... a1[N]-a0[N]
    *     ...
    *
    * Note that many of the components on a single row above are likely to be
    * replicated multiple times (if, say, a single SIMD thread is only
    * processing 2 different polygons), so plane parameters aren't actually
    * stored in GRF memory with that layout to avoid wasting space. Instead we
    * compose ATTR register regions with a 2D region that walks through the
    * parameters of each polygon with the correct stride, reading the
    * parameter corresponding to each channel directly from the PS thread
    * payload.
    *
    * The latter layout corresponds to a param_width equal to dispatch_width,
    * while the former (scalar parameter) layout has a param_width of 1.
    *
    * Gfx20+ represent plane parameters in a format similar to the above,
    * except the parameters are packed in 12B and ordered like "a0, a1-a0,
    * a2-a0" instead of the above vec4 representation with a missing
    * component.
    *
    * First documented in the TGL PRMs, Volume 9: Render Engine, PS Thread
    * Payload for Normal Dispatch.
    *
    * Pre Xe2 : BSpec 47024
    * Xe2+    : BSpec 56480
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
   const bool per_prim = src.nr < prog_data->num_per_primitive_inputs;
   const unsigned base = urb_start +
      (per_prim ? 0 :
       align(prog_data->num_per_primitive_inputs / 2,
             reg_unit(s.devinfo)) * s.max_polygons);
   const unsigned idx = per_prim ? src.nr :
      src.nr - prog_data->num_per_primitive_inputs;

   /* Translate the offset within the param_width-wide
    * representation described above into an offset and a
    * grf, which contains the plane parameters for the first
    * polygon processed by the thread.
    */
   if (s.devinfo->ver >= 20 && !per_prim) {
      /* Gfx20+ is able to pack 5 logical input components
       * per 64B register for vertex setup data.
       */
      const unsigned grf = base + idx / 5 * 2 * s.max_polygons;
      assert(src.offset / param_width < 12);
      const unsigned delta = idx % 5 * 12 +
         src.offset / (param_width * chan_sz) * chan_sz +
         src.offset % chan_sz;
      reg = byte_offset(retype(brw_vec8_grf(grf, 0), src.type), delta);
   } else {
      /* Earlier platforms and per-primitive block pack 2 logical
       * input components per 32B register.
       */
      const unsigned grf = base + idx / 2 * s.max_polygons;
      assert(reg.offset / param_width < REG_SIZE / 2);
      const unsigned delta = (idx % 2) * (REG_SIZE / 2) +
         src.offset / (param_width * chan_sz) * chan_sz +
         src.offset % chan_sz;
      reg = byte_offset(retype(brw_vec8_grf(grf, 0), src.type), delta);
   }

   if (s.max_polygons > 1) {
      assert(s.devinfo->ver >= 12);
      /* Misaligned channel strides that would lead to
       * cross-channel access in the representation above are
       * disallowed.
       */
      assert(src.stride * brw_type_size_bytes(src.type) == chan_sz);

      /* Number of channels processing the same polygon. */
      const unsigned poly_width = s.dispatch_width / s.max_polygons;
      assert(s.dispatch_width % s.max_polygons == 0);

      /* Accessing a subset of channels of a parameter vector
       * starting from "chan" is necessary to handle
       * SIMD-lowered instructions though.
       */
      const unsigned chan = src.offset %
         (param_width * chan_sz) / chan_sz;
      assert(chan < s.dispatch_width);
      assert(chan % poly_width == 0);
      const unsigned reg_size = reg_unit(s.devinfo) * REG_SIZE;
      reg = byte_offset(reg, chan / poly_width * reg_size);

      if (exec_size > poly_width) {
         /* Accessing the parameters for multiple polygons.
          * Corresponding parameters for different polygons
          * are stored a GRF apart on the thread payload, so
          * use that as vertical stride.
          */
         const unsigned vstride = reg_size / brw_type_size_bytes(src.type);
         assert(vstride <= 32);
         assert(chan % poly_width == 0);
         reg = stride(reg, vstride, poly_width, 0);
      } else {
         /* Accessing one parameter for a single polygon --
          * Translate to a scalar region.
          */
         assert(chan % poly_width + exec_size <= poly_width);
         reg = stride(reg, 0, 1, 0);
      }

   } else {
      const unsigned width = src.stride == 0 ? 1 : MIN2(exec_size, 8);
      reg = stride(reg, width * src.stride, width, src.stride);
   }

   reg.abs = src.abs;
   reg.negate = src.negate;

   return reg;
}

static void
brw_assign_urb_setup(brw_shader &s)
{
   assert(s.stage == MESA_SHADER_FRAGMENT);

   struct brw_wm_prog_data *prog_data = brw_wm_prog_data(s.prog_data);

   int urb_start = s.payload().num_regs + prog_data->base.curb_read_length;
   bool read_attribute_payload = false;

   /* Offset all the urb_setup[] index by the actual position of the
    * setup regs, now that the location of the constants has been chosen.
    */
   foreach_block_and_inst_safe(block, brw_inst, inst, s.cfg) {
      if (inst->opcode == SHADER_OPCODE_LOAD_ATTRIBUTE_PAYLOAD) {
         brw_reg offset = inst->src[0];
         inst = brw_transform_inst(s, inst, SHADER_OPCODE_MOV_INDIRECT, 3);
         inst->src[0] = retype(brw_vec8_grf(urb_start, 0), BRW_TYPE_UD);
         inst->src[1] = offset;
         inst->src[2] = brw_imm_ud(REG_SIZE * 2 * 32);
         read_attribute_payload = true;
         continue;
      }

      if (inst->dst.file == ATTR) {
         inst->dst = remap_attr_reg(s, prog_data, inst->dst,
                                    urb_start, inst->exec_size);
         continue;
      }

      for (int i = 0; i < inst->sources; i++) {
         if (inst->src[i].file == ATTR) {
            inst->src[i] = remap_attr_reg(s, prog_data, inst->src[i],
                                          urb_start, inst->exec_size);
         }
      }
   }

   if (read_attribute_payload) {
      s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS |
                            BRW_DEPENDENCY_VARIABLES);
   }

   /* Each attribute is 4 setup channels, each of which is half a reg,
    * but they may be replicated multiple times for multipolygon
    * dispatch.
    */
   s.first_non_payload_grf +=
      (read_attribute_payload ? 32 : prog_data->num_varying_inputs) *
      2 * s.max_polygons;

   /* Unlike regular attributes, per-primitive attributes have all 4 channels
    * in the same slot, so each GRF can store two slots.
    */
   assert(prog_data->num_per_primitive_inputs % 2 == 0);
   s.first_non_payload_grf += prog_data->num_per_primitive_inputs / 2 * s.max_polygons;

   s.debug_optimizer(s.nir, "urb_setup", 91, 0);
}

static bool
run_fs(brw_shader &s, bool allow_spilling, bool do_rep_send)
{
   const struct intel_device_info *devinfo = s.devinfo;
   struct brw_wm_prog_data *wm_prog_data = brw_wm_prog_data(s.prog_data);
   const brw_builder bld = brw_builder(&s);
   const nir_shader *nir = s.nir;

   assert(s.stage == MESA_SHADER_FRAGMENT);

   s.payload_ = new brw_fs_thread_payload(s, s.source_depth_to_render_target);

   if (nir->info.ray_queries > 0)
      s.limit_dispatch_width(16, "SIMD32 not supported with ray queries.\n");

   if (do_rep_send) {
      assert(s.dispatch_width == 16);
      brw_emit_repclear_shader(s);
   } else {
      if (nir->info.inputs_read > 0 ||
          BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_FRAG_COORD) ||
          (nir->info.outputs_read > 0 && !brw_can_coherent_fb_fetch(devinfo))) {
         brw_emit_interpolation_setup(s);
      }

      /* We handle discards by keeping track of the still-live pixels in f0.1.
       * On Xe2+, we also predicate stores and test helper invocations with
       * this mask. Initialize it with the dispatched pixels if we use discard
       * or (on Xe2) memory stores or helper invocation testing.
       */
      if ((devinfo->ver >= 20 &&
           (BITSET_TEST(nir->info.system_values_read,
                        SYSTEM_VALUE_HELPER_INVOCATION) ||
            nir->info.writes_memory)) ||
          wm_prog_data->uses_kill) {

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
            bld.uniform().MOV(brw_sample_mask_reg(bld.group(lower_width, i)),
                              retype(dispatch_mask, BRW_TYPE_UW));
         }
      }

      if (nir->info.writes_memory)
         wm_prog_data->has_side_effects = true;

      brw_from_nir(&s);

      if (s.failed)
	 return false;

      brw_emit_fb_writes(s);
      if (s.failed)
	 return false;

      brw_calculate_cfg(s);

      brw_optimize(s);

      s.assign_curb_setup();

      if (devinfo->ver == 9)
         gfx9_ps_header_only_workaround(wm_prog_data);

      brw_assign_urb_setup(s);

      brw_lower_3src_null_dest(s);
      brw_workaround_emit_dummy_mov_instruction(s);

      brw_allocate_registers(s, allow_spilling);

      brw_workaround_source_arf_before_eot(s);
   }

   return !s.failed;
}

static void
brw_print_fs_urb_setup(FILE *fp, const struct brw_wm_prog_data *prog_data,
                       int *per_primitive_offsets)
{
   fprintf(fp, "FS URB (inputs=0x%016" PRIx64 ", flat_inputs=0x%08x):\n",
           prog_data->inputs, prog_data->flat_inputs);
   fprintf(fp, "  URB setup:\n");
   for (uint32_t i = 0; i < ARRAY_SIZE(prog_data->urb_setup); i++) {
      if (prog_data->urb_setup[i] >= 0) {
         fprintf(fp, "   [%02d]: %i channel=%u (%s)\n",
                 i, prog_data->urb_setup[i], prog_data->urb_setup_channel[i],
                 gl_varying_slot_name_for_stage((gl_varying_slot)i,
                                                MESA_SHADER_FRAGMENT));
      }
   }
   fprintf(fp, "  URB setup attributes:\n");
   for (uint32_t i = 0; i < prog_data->urb_setup_attribs_count; i++) {
      fprintf(fp, "   [%02d]: %i (%s)\n",
              i, prog_data->urb_setup_attribs[i],
              gl_varying_slot_name_for_stage(
                 (gl_varying_slot)prog_data->urb_setup_attribs[i],
                 MESA_SHADER_FRAGMENT));
   }
   if (per_primitive_offsets) {
      fprintf(fp, "  Per Primitive URB setup:\n");
      for (uint32_t i = 0; i < VARYING_SLOT_MAX; i++) {
         if (per_primitive_offsets[i] == -1 ||
             i == VARYING_SLOT_PRIMITIVE_COUNT ||
             i == VARYING_SLOT_PRIMITIVE_INDICES)
            continue;
         fprintf(fp, "   [%02d]: %i (%s)\n",
                 i, per_primitive_offsets[i],
                 gl_varying_slot_name_for_stage((gl_varying_slot)i,
                                                MESA_SHADER_FRAGMENT));
      }
   }
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
                                   params->base.debug_flag : DEBUG_WM,
                                   params->base.source_hash);

   brw_prog_data_init(&prog_data->base, &params->base);

   const struct intel_device_info *devinfo = compiler->devinfo;
   const unsigned max_subgroup_size = 32;
   unsigned max_polygons = MAX2(1, params->max_polygons);

   brw_debug_archive_nir(params->base.archiver, nir, 0, "first");

   brw_nir_apply_key(nir, compiler, &key->base, max_subgroup_size);

   if (brw_nir_fragment_shader_needs_wa_18019110168(devinfo, key->mesh_input, nir)) {
      if (params->mue_map && params->mue_map->wa_18019110168_active) {
         brw_nir_frag_convert_attrs_prim_to_vert(
            nir, params->mue_map->per_primitive_offsets);
      } else {
         NIR_PASS(_, nir, brw_nir_frag_convert_attrs_prim_to_vert_indirect,
                  devinfo, params);
      }
      /* Remapping per-primitive inputs into unused per-vertex inputs cannot
       * work with multipolygon.
       */
      max_polygons = 1;
   }

   brw_nir_lower_fs_inputs(nir, devinfo, key);
   brw_nir_lower_fs_outputs(nir);

   if (!brw_can_coherent_fb_fetch(devinfo))
      NIR_PASS(_, nir, brw_nir_lower_fs_load_output, key);

   NIR_PASS(_, nir, nir_opt_frag_coord_to_pixel_coord);
   NIR_PASS(_, nir, nir_lower_frag_coord_to_pixel_coord);

   /* From the SKL PRM, Volume 7, "Alpha Coverage":
    *  "If Pixel Shader outputs oMask, AlphaToCoverage is disabled in
    *   hardware, regardless of the state setting for this feature."
    */
   if (key->alpha_to_coverage != INTEL_NEVER) {
      /* Run constant fold optimization in order to get the correct source
       * offset to determine render target 0 store instruction in
       * emit_alpha_to_coverage pass.
       */
      NIR_PASS(_, nir, nir_opt_constant_folding);
      NIR_PASS(_, nir, brw_nir_lower_alpha_to_coverage);
   }

   NIR_PASS(_, nir, brw_nir_move_interpolation_to_top);

   if (!brw_wm_prog_key_is_dynamic(key)) {
      uint32_t f = 0;

      if (key->multisample_fbo == INTEL_ALWAYS)
         f |= INTEL_MSAA_FLAG_MULTISAMPLE_FBO;

      if (key->alpha_to_coverage == INTEL_ALWAYS)
         f |= INTEL_MSAA_FLAG_ALPHA_TO_COVERAGE;

      if (key->provoking_vertex_last == INTEL_ALWAYS)
         f |= INTEL_MSAA_FLAG_PROVOKING_VERTEX_LAST;

      if (key->persample_interp == INTEL_ALWAYS) {
         f |= INTEL_MSAA_FLAG_PERSAMPLE_DISPATCH |
              INTEL_MSAA_FLAG_PERSAMPLE_INTERP;
      }

      NIR_PASS(_, nir, nir_inline_sysval, nir_intrinsic_load_fs_msaa_intel, f);
   }

   brw_postprocess_nir_opts(nir, compiler, key->base.robust_flags);

   unsigned pressure[SIMD_COUNT];
   brw_nir_quick_pressure_estimate(nir, devinfo, pressure);

   bool beyond_threshold[SIMD_COUNT] = {};
   for (unsigned i = (devinfo->ver >= 20 ? 2 : 1); i < SIMD_COUNT; i++) {
      beyond_threshold[i] =
         pressure[i] > compiler->register_pressure_threshold;
   }

   brw_postprocess_nir_out_of_ssa(nir, 0, params->base.archiver,
                                  debug_enabled);

   int per_primitive_offsets[VARYING_SLOT_MAX];
   memset(per_primitive_offsets, -1, sizeof(per_primitive_offsets));

   brw_nir_populate_wm_prog_data(nir, compiler->devinfo, key, prog_data,
                                 params->mue_map,
                                 per_primitive_offsets);

   if (unlikely(debug_enabled))
      brw_print_fs_urb_setup(stderr, prog_data, per_primitive_offsets);

   /* Either an unrestricted or a fixed SIMD16 subgroup size are
    * allowed -- The latter is needed for fast clear and replicated
    * data clear shaders.
    */
   const unsigned reqd_dispatch_width = brw_required_dispatch_width(&nir->info);
   assert(reqd_dispatch_width == 0 || reqd_dispatch_width == 16);

   /* Limit identified when first variant is compiled, see
    * brw_shader::limit_dispatch_width().
    */
   unsigned dispatch_width_limit = UINT_MAX;

   std::unique_ptr<brw_shader> v8, v16, v32, vmulti;
   float throughput = 0;
   bool has_spilled = false;

   const brw_shader_params base_shader_params = {
      .compiler                = compiler,
      .mem_ctx                 = params->base.mem_ctx,
      .nir                     = nir,
      .key                     = &key->base,
      .prog_data               = &prog_data->base,
      .per_primitive_offsets   = per_primitive_offsets,
      .needs_register_pressure = params->base.stats != NULL,
      .log_data                = params->base.log_data,
      .debug_enabled           = debug_enabled,
      .archiver                = params->base.archiver,
   };

   if (devinfo->ver < 20) {
      brw_shader_params shader_params = base_shader_params;
      shader_params.dispatch_width = 8;
      shader_params.num_polygons   = 1;

      v8 = std::make_unique<brw_shader>(&shader_params);
      if (!run_fs(*v8, allow_spilling, false /* do_rep_send */)) {
         params->base.error_str = ralloc_strdup(params->base.mem_ctx,
                                                v8->fail_msg);
         return NULL;
      }

      if (key->coarse_pixel) {
         if (prog_data->dual_src_blend) {
            v8->limit_dispatch_width(8, "SIMD16 coarse pixel shading cannot"
                                     " use SIMD8 messages.\n");
         }
         v8->limit_dispatch_width(16, "SIMD32 not supported with coarse"
                                  " pixel shading.\n");
      }

      dispatch_width_limit = MIN2(dispatch_width_limit, v8->max_dispatch_width);

      if (INTEL_SIMD(FS, 8)) {
         assert(v8->payload().num_regs % reg_unit(devinfo) == 0);
         prog_data->base.dispatch_grf_start_reg = v8->payload().num_regs / reg_unit(devinfo);
         prog_data->base.grf_used = MAX2(prog_data->base.grf_used,
                                         v8->grf_used);

         const brw_performance &perf = v8->performance_analysis.require();
         throughput = MAX2(throughput, perf.throughput);
         has_spilled = v8->spilled_any_registers;
         allow_spilling = false;
      } else {
         /* Not using SIMD8. */
         v8.reset();
      }
   }

   if (compiler->optimistic_simd_heuristic) {
      unsigned max_dispatch_width = reqd_dispatch_width ? reqd_dispatch_width : 32;

      if (max_polygons >= 2 && !key->coarse_pixel) {
         if (max_polygons >= 4 && max_dispatch_width >= 32 &&
             4 * prog_data->num_varying_inputs <= MAX_VARYING &&
             INTEL_SIMD(FS, 4X8)) {
            /* Try a quad-SIMD8 compile */
            brw_shader_params shader_params = base_shader_params;
            shader_params.dispatch_width = 32;
            shader_params.num_polygons   = 4;
            vmulti = std::make_unique<brw_shader>(&shader_params);
            max_dispatch_width = std::min(max_dispatch_width, vmulti->dispatch_width);

            if (!run_fs(*vmulti, false, false)) {
               brw_shader_perf_log(compiler, params->base.log_data,
                                   "Quad-SIMD8 shader failed to compile: %s\n",
                                   vmulti->fail_msg);
               vmulti.reset();
            } else {
               assert(!vmulti->spilled_any_registers);
            }
         }

         if (!vmulti && max_dispatch_width >= 32 &&
             2 * prog_data->num_varying_inputs <= MAX_VARYING &&
             INTEL_SIMD(FS, 2X16)) {
            /* Try a dual-SIMD16 compile */
            brw_shader_params shader_params = base_shader_params;
            shader_params.dispatch_width = 32;
            shader_params.num_polygons   = 2;

            vmulti = std::make_unique<brw_shader>(&shader_params);
            max_dispatch_width = std::min(max_dispatch_width, vmulti->dispatch_width);

            if (!run_fs(*vmulti, false, false)) {
               brw_shader_perf_log(compiler, params->base.log_data,
                                   "Dual-SIMD16 shader failed to compile: %s\n",
                                   vmulti->fail_msg);
               vmulti.reset();
            } else {
               assert(!vmulti->spilled_any_registers);
            }
         }

         if (!vmulti && max_dispatch_width >= 16 &&
             2 * prog_data->num_varying_inputs <= MAX_VARYING &&
             INTEL_SIMD(FS, 2X8)) {
            /* Try a dual-SIMD8 compile */
            brw_shader_params shader_params = base_shader_params;
            shader_params.dispatch_width = 16;
            shader_params.num_polygons   = 2;

            vmulti = std::make_unique<brw_shader>(&shader_params);
            max_dispatch_width = std::min(max_dispatch_width, vmulti->dispatch_width);

            if (!run_fs(*vmulti, false, false)) {
               brw_shader_perf_log(compiler, params->base.log_data,
                                   "Dual-SIMD8 shader failed to compile: %s\n",
                                   vmulti->fail_msg);
               vmulti.reset();
            }
         }
      }

      if ((!vmulti || vmulti->dispatch_width < 32) &&
          max_dispatch_width >= 32 &&
          INTEL_SIMD(FS, 32) &&
          !prog_data->base.ray_queries) {
         /* Try a SIMD32 compile */
         brw_shader_params shader_params = base_shader_params;
         shader_params.dispatch_width = 32;
         shader_params.num_polygons   = 1;

         v32 = std::make_unique<brw_shader>(&shader_params);

         if (!run_fs(*v32, false, false)) {
            brw_shader_perf_log(compiler, params->base.log_data,
                                "SIMD32 shader failed to compile: %s\n",
                                v32->fail_msg);
            v32.reset();
         } else {
            assert(v32->payload().num_regs % reg_unit(devinfo) == 0);
            prog_data->dispatch_grf_start_reg_32 = v32->payload().num_regs / reg_unit(devinfo);
            prog_data->base.grf_used = MAX2(prog_data->base.grf_used,
                                            v32->grf_used);
         }
      }

      if (!vmulti && !v32 && INTEL_SIMD(FS, 16)) {
         /* Try a SIMD16 compile */
         brw_shader_params shader_params = base_shader_params;
         shader_params.dispatch_width = 16;
         shader_params.num_polygons   = 1;
         v16 = std::make_unique<brw_shader>(&shader_params);

         if (!run_fs(*v16, allow_spilling, params->use_rep_send)) {
            brw_shader_perf_log(compiler, params->base.log_data,
                                "SIMD16 shader failed to compile: %s\n",
                                v16->fail_msg);
            v16.reset();
         } else {
            assert(v16->payload().num_regs % reg_unit(devinfo) == 0);
            prog_data->dispatch_grf_start_reg_16 = v16->payload().num_regs / reg_unit(devinfo);
            prog_data->base.grf_used = MAX2(prog_data->base.grf_used,
                                            v16->grf_used);
         }
      }

   } else {
      if ((!has_spilled && dispatch_width_limit >= 16 &&
           !beyond_threshold[1] && INTEL_SIMD(FS, 16)) ||
          reqd_dispatch_width == 16) {
         /* Try a SIMD16 compile */
         brw_shader_params shader_params = base_shader_params;
         shader_params.dispatch_width = 16;
         shader_params.num_polygons   = 1;

         v16 = std::make_unique<brw_shader>(&shader_params);

         if (!run_fs(*v16, allow_spilling, params->use_rep_send)) {
            brw_shader_perf_log(compiler, params->base.log_data,
                                "SIMD16 shader failed to compile: %s\n",
                                v16->fail_msg);
            v16.reset();
         } else {
            dispatch_width_limit = MIN2(dispatch_width_limit, v16->max_dispatch_width);

            assert(v16->payload().num_regs % reg_unit(devinfo) == 0);
            prog_data->dispatch_grf_start_reg_16 = v16->payload().num_regs / reg_unit(devinfo);
            prog_data->base.grf_used = MAX2(prog_data->base.grf_used,
                                            v16->grf_used);

            const brw_performance &perf = v16->performance_analysis.require();
            throughput = MAX2(throughput, perf.throughput);
            has_spilled = v16->spilled_any_registers;
            allow_spilling = false;
         }
      }

      const bool simd16_failed = !v16;

      /* Currently, the compiler only supports SIMD32 on SNB+ */
      if (!has_spilled &&
          dispatch_width_limit >= 32 &&
          !beyond_threshold[2] &&
          reqd_dispatch_width == 0 &&
          !simd16_failed && INTEL_SIMD(FS, 32) &&
          !prog_data->base.ray_queries) {
         /* Try a SIMD32 compile */
         brw_shader_params shader_params = base_shader_params;
         shader_params.dispatch_width = 32;
         shader_params.num_polygons   = 1;

         v32 = std::make_unique<brw_shader>(&shader_params);

         if (!run_fs(*v32, allow_spilling, false)) {
            brw_shader_perf_log(compiler, params->base.log_data,
                                "SIMD32 shader failed to compile: %s\n",
                                v32->fail_msg);
            v32.reset();
         } else {
            const brw_performance &perf = v32->performance_analysis.require();

            if (!INTEL_DEBUG(DEBUG_DO32) && throughput >= perf.throughput) {
               brw_shader_perf_log(compiler, params->base.log_data,
                                   "SIMD32 shader inefficient\n");
               v32.reset();
            } else {
               assert(v32->payload().num_regs % reg_unit(devinfo) == 0);
               prog_data->dispatch_grf_start_reg_32 = v32->payload().num_regs / reg_unit(devinfo);
               prog_data->base.grf_used = MAX2(prog_data->base.grf_used,
                                               v32->grf_used);

               throughput = MAX2(throughput, perf.throughput);
            }
         }
      }

      if (devinfo->ver >= 12 && !has_spilled &&
          max_polygons >= 2 && !key->coarse_pixel &&
          reqd_dispatch_width == 0) {

         if (devinfo->ver >= 20 && max_polygons >= 4 &&
             dispatch_width_limit >= 32 && !beyond_threshold[2] &&
             4 * prog_data->num_varying_inputs <= MAX_VARYING &&
             INTEL_SIMD(FS, 4X8)) {
            /* Try a quad-SIMD8 compile */
            brw_shader_params shader_params = base_shader_params;
            shader_params.dispatch_width = 32;
            shader_params.num_polygons   = 4;
            vmulti = std::make_unique<brw_shader>(&shader_params);

            if (!run_fs(*vmulti, false, params->use_rep_send)) {
               brw_shader_perf_log(compiler, params->base.log_data,
                                   "Quad-SIMD8 shader failed to compile: %s\n",
                                   vmulti->fail_msg);
               vmulti.reset();
            } else {
               assert(!vmulti->spilled_any_registers);
            }
         }

         if (!vmulti && devinfo->ver >= 20 &&
             dispatch_width_limit >= 32 && !beyond_threshold[2] &&
             2 * prog_data->num_varying_inputs <= MAX_VARYING &&
             INTEL_SIMD(FS, 2X16)) {
            /* Try a dual-SIMD16 compile */
            brw_shader_params shader_params = base_shader_params;
            shader_params.dispatch_width = 32;
            shader_params.num_polygons   = 2;

            vmulti = std::make_unique<brw_shader>(&shader_params);
            if (!run_fs(*vmulti, false, params->use_rep_send)) {
               brw_shader_perf_log(compiler, params->base.log_data,
                                   "Dual-SIMD16 shader failed to compile: %s\n",
                                   vmulti->fail_msg);
               vmulti.reset();
            } else {
               assert(!vmulti->spilled_any_registers);
            }
         }

         if (!vmulti && dispatch_width_limit >= 16 && !beyond_threshold[1] &&
             2 * prog_data->num_varying_inputs <= MAX_VARYING &&
             INTEL_SIMD(FS, 2X8)) {
            /* Try a dual-SIMD8 compile */
            brw_shader_params shader_params = base_shader_params;
            shader_params.dispatch_width = 16;
            shader_params.num_polygons   = 2;

            vmulti = std::make_unique<brw_shader>(&shader_params);

            if (!run_fs(*vmulti, allow_spilling, params->use_rep_send)) {
               brw_shader_perf_log(compiler, params->base.log_data,
                                   "Dual-SIMD8 shader failed to compile: %s\n",
                                   vmulti->fail_msg);
               vmulti.reset();
            }
         }
      }
   }

   if (vmulti) {
      assert(vmulti->payload().num_regs % reg_unit(devinfo) == 0);
      prog_data->base.dispatch_grf_start_reg = vmulti->payload().num_regs / reg_unit(devinfo);
      prog_data->base.grf_used = MAX2(prog_data->base.grf_used,
                                      vmulti->grf_used);
   }

   /* When the caller compiles a repclear or fast clear shader, they
    * want SIMD16-only.
    */
   if (reqd_dispatch_width == 16)
      v8.reset();

   brw_generator g(compiler, &params->base, &prog_data->base,
                  MESA_SHADER_FRAGMENT);

   if (unlikely(debug_enabled)) {
      g.enable_debug(ralloc_asprintf(params->base.mem_ctx,
                                     "%s fragment shader %s",
                                     nir->info.label ?
                                        nir->info.label : "unnamed",
                                     nir->info.name));
   }

   struct genisa_stats *stats = params->base.stats;
   uint32_t max_dispatch_width = 0;

   if (vmulti) {
      prog_data->dispatch_multi = vmulti->dispatch_width;
      prog_data->max_polygons = vmulti->max_polygons;
      g.generate_code(*vmulti, stats);
      stats = stats ? stats + 1 : NULL;
      max_dispatch_width = vmulti->dispatch_width;
   } else if (v8) {
      prog_data->dispatch_8 = true;
      g.generate_code(*v8, stats);
      stats = stats ? stats + 1 : NULL;
      max_dispatch_width = 8;
   }

   if (v16) {
      prog_data->dispatch_16 = true;
      prog_data->prog_offset_16 = g.generate_code(*v16, stats);
      stats = stats ? stats + 1 : NULL;
      max_dispatch_width = 16;
   }

   if (v32) {
      prog_data->dispatch_32 = true;
      prog_data->prog_offset_32 = g.generate_code(*v32, stats);
      stats = stats ? stats + 1 : NULL;
      max_dispatch_width = 32;
   }

   for (struct genisa_stats *s = params->base.stats; s != NULL && s != stats; s++)
      s->max_dispatch_width = max_dispatch_width;

   g.add_const_data(nir->constant_data, nir->constant_data_size);
   return g.get_assembly();
}

extern "C" void
brw_compute_sbe_per_vertex_urb_read(const struct intel_vue_map *prev_stage_vue_map,
                                    bool mesh,
                                    bool per_primitive_remapping,
                                    const struct brw_wm_prog_data *wm_prog_data,
                                    uint32_t *out_read_offset,
                                    uint32_t *out_read_length,
                                    uint32_t *out_num_varyings,
                                    uint32_t *out_primitive_id_offset,
                                    uint32_t *out_flat_inputs)
{
   int first_slot = INT32_MAX, last_slot = -1;

   /* Ignore PrimitiveID in mesh pipelines, this value is coming from the
    * per-primitive block.
    */
   uint64_t inputs_read = wm_prog_data->inputs;
   if (mesh)
      inputs_read &= ~VARYING_BIT_PRIMITIVE_ID;

   for (int _i = 0; _i < prev_stage_vue_map->num_slots; _i++) {
      uint32_t i = prev_stage_vue_map->num_slots - 1 - _i;
      int varying = prev_stage_vue_map->slot_to_varying[i];
      if (varying < 0)
         continue;

      if (varying == BRW_VARYING_SLOT_PAD ||
          (inputs_read & BITFIELD64_BIT(varying)) == 0)
         continue;

      last_slot = i;
      break;
   }

   for (int i = 0; i < prev_stage_vue_map->num_slots; i++) {
      int varying = prev_stage_vue_map->slot_to_varying[i];
      if (varying != BRW_VARYING_SLOT_PAD && varying > 0 &&
          (inputs_read & BITFIELD64_BIT(varying)) != 0) {
         first_slot = i;
         break;
      }
   }

   assert((first_slot == INT32_MAX && last_slot == -1) ||
          (first_slot >= 0 && last_slot >= 0 && last_slot >= first_slot));

   uint32_t num_varyings = wm_prog_data->num_varying_inputs;
   uint32_t remapped_flat_inputs = 0;

   /* When using INTEL_VUE_LAYOUT_SEPARATE_MESH, the location of the
    * PrimitiveID is unknown at compile time, here we compute the offset
    * inside the attribute registers which will be read with MOV_INDIRECT in
    * the shader.
    */
   *out_primitive_id_offset = 0;
   if (prev_stage_vue_map->layout == INTEL_VUE_LAYOUT_SEPARATE_MESH) {
      if (per_primitive_remapping && wm_prog_data->per_primitive_inputs != 0) {
         /* When the mesh shader remaps per-primitive slots to per-vertex
          * ones, read the entire set of slots.
          */
         assert(mesh);
         remapped_flat_inputs =
            ((1u << prev_stage_vue_map->num_slots) - 1) &
            ~((1u << last_slot) - 1);
         *out_flat_inputs |= remapped_flat_inputs;
         last_slot = prev_stage_vue_map->num_slots - 1;
         *out_primitive_id_offset = INTEL_MSAA_FLAG_PRIMITIVE_ID_INDEX_MESH;
         num_varyings = prev_stage_vue_map->num_slots - first_slot;
      } else if (mesh) {
         /* When using Mesh, the PrimitiveID is in the per-primitive block. */
         if (wm_prog_data->urb_setup[VARYING_SLOT_PRIMITIVE_ID] >= 0)
            num_varyings--;
         *out_primitive_id_offset = INTEL_MSAA_FLAG_PRIMITIVE_ID_INDEX_MESH;
      } else if (inputs_read & VARYING_BIT_PRIMITIVE_ID) {
         int primitive_id_slot;
         if (prev_stage_vue_map->varying_to_slot[VARYING_SLOT_PRIMITIVE_ID] < 0) {
            /* If the previous stage doesn't write PrimitiveID, we can have
             * the HW generate a value (except if GS is enabled but in that
             * case that's undefined).
             *
             * If the FS shader already has a slot of the PrimitiveID value,
             * use that.
             */
            if (wm_prog_data->urb_setup[VARYING_SLOT_PRIMITIVE_ID] >= 0) {
               if (first_slot == INT32_MAX) {
                  first_slot =
                     wm_prog_data->urb_setup[VARYING_SLOT_PRIMITIVE_ID];
               }
               /* urb_setup[VARYING_SLOT_PRIMITIVE_ID] is relative to the
                * first read slot, so bring primitive_id_slot back into the
                * absolute indexing of the VUE.
                */
               primitive_id_slot = first_slot +
                  wm_prog_data->urb_setup[VARYING_SLOT_PRIMITIVE_ID];
            } else {
               primitive_id_slot = ++last_slot;
            }
         } else {
            primitive_id_slot =
               prev_stage_vue_map->varying_to_slot[VARYING_SLOT_PRIMITIVE_ID];
         }
         first_slot = MIN2(primitive_id_slot, first_slot);
         last_slot = MAX2(primitive_id_slot, last_slot);

         *out_primitive_id_offset = primitive_id_slot - first_slot;
         /* Make sure to have constant interpolation on PrimitiveID */
         remapped_flat_inputs |= BITFIELD_BIT(*out_primitive_id_offset);
      }
   }

   /* Compute the read parameters for SBE (those have to be 32B aligned) */
   if (last_slot == -1) {
      *out_read_offset = 0;
      *out_read_length = DIV_ROUND_UP(num_varyings, 2);
      *out_num_varyings = num_varyings;
   } else {
      first_slot = ROUND_DOWN_TO(first_slot, 2);
      *out_read_offset = first_slot / 2;
      *out_read_length = DIV_ROUND_UP(last_slot - first_slot + 1, 2);
      *out_num_varyings = num_varyings;
   }

   *out_flat_inputs = wm_prog_data->flat_inputs | remapped_flat_inputs;
}

extern "C" void
brw_compute_sbe_per_primitive_urb_read(uint64_t inputs_read,
                                       uint32_t num_varyings,
                                       const struct brw_mue_map *mue_map,
                                       uint32_t *out_read_offset,
                                       uint32_t *out_read_length)
{
   /* The header slots are irrelevant for the URB varying slots. They are
    * delivered somewhere else in the thread payload.
    *
    * For example on DG2:
    *   - PRIMITIVE_SHADING_RATE : R1.0, ActualCoarsePixelShadingSize.(X|Y)
    *   - LAYER                  : R1.1, Render Target Array Index
    *   - VIEWPORT               : R1.1, Viewport Index
    *   - PSIZ                   : not available in fragment shaders
    *   - FACE                   : R1.1, Front/Back Facing
    */
   inputs_read &= ~(BRW_VUE_HEADER_VARYING_MASK | VARYING_BIT_FACE);

   uint32_t first_read = UINT32_MAX;
   u_foreach_bit64(varying, inputs_read) {
      if (mue_map->per_primitive_offsets[varying] < 0)
         continue;

      first_read = mue_map->per_primitive_offsets[varying];
      break;
   }

   /* Not loading any per-primitive data in this case, the push constants
    * should be adjusted though.
    */
   if (mue_map->wa_18019110168_active) {
      *out_read_offset = *out_read_length = 0;
   } else {
      *out_read_offset = DIV_ROUND_UP(first_read, 32);
      *out_read_length = DIV_ROUND_UP(num_varyings, 2);
   }
}
