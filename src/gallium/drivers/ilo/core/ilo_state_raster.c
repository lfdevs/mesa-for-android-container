/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2012-2015 LunarG, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Chia-I Wu <olv@lunarg.com>
 */

#include "ilo_debug.h"
#include "ilo_state_raster.h"

static bool
raster_validate_gen6_clip(const struct ilo_dev *dev,
                          const struct ilo_state_raster_info *info)
{
   const struct ilo_state_raster_clip_info *clip = &info->clip;

   ILO_DEV_ASSERT(dev, 6, 8);

   assert(clip->viewport_count);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 188:
    *
    *     ""Clip Distance Cull Test Enable Bitmask" and "Clip Distance Clip
    *      Test Enable Bitmask" should not have overlapping bits in the mask,
    *      else the results are undefined."
    */
   assert(!(clip->user_cull_enables & clip->user_clip_enables));

   if (ilo_dev_gen(dev) < ILO_GEN(9))
      assert(clip->z_near_enable == clip->z_far_enable);

   return true;
}

static bool
raster_set_gen6_3DSTATE_CLIP(struct ilo_state_raster *rs,
                             const struct ilo_dev *dev,
                             const struct ilo_state_raster_info *info)
{
   const struct ilo_state_raster_clip_info *clip = &info->clip;
   const struct ilo_state_raster_setup_info *setup = &info->setup;
   const struct ilo_state_raster_tri_info *tri = &info->tri;
   const struct ilo_state_raster_scan_info *scan = &info->scan;
   uint32_t dw1, dw2, dw3;

   ILO_DEV_ASSERT(dev, 6, 8);

   if (!raster_validate_gen6_clip(dev, info))
      return false;

   dw1 = clip->user_cull_enables << GEN6_CLIP_DW1_UCP_CULL_ENABLES__SHIFT;

   if (clip->stats_enable)
      dw1 |= GEN6_CLIP_DW1_STATISTICS;

   if (ilo_dev_gen(dev) >= ILO_GEN(7)) {
      /*
       * From the Ivy Bridge PRM, volume 2 part 1, page 219:
       *
       *     "Workaround : Due to Hardware issue "EarlyCull" needs to be
       *      enabled only for the cases where the incoming primitive topology
       *      into the clipper guaranteed to be Trilist."
       *
       * What does this mean?
       */
      dw1 |= GEN7_CLIP_DW1_SUBPIXEL_8BITS |
             GEN7_CLIP_DW1_EARLY_CULL_ENABLE;

      if (ilo_dev_gen(dev) <= ILO_GEN(7.5)) {
         dw1 |= tri->front_winding << GEN7_CLIP_DW1_FRONT_WINDING__SHIFT |
                tri->cull_mode << GEN7_CLIP_DW1_CULL_MODE__SHIFT;
      }
   }

   dw2 = clip->user_clip_enables << GEN6_CLIP_DW2_UCP_CLIP_ENABLES__SHIFT |
         GEN6_CLIPMODE_NORMAL << GEN6_CLIP_DW2_CLIP_MODE__SHIFT;

   if (clip->clip_enable)
      dw2 |= GEN6_CLIP_DW2_CLIP_ENABLE;

   if (clip->z_near_zero)
      dw2 |= GEN6_CLIP_DW2_APIMODE_D3D;
   else
      dw2 |= GEN6_CLIP_DW2_APIMODE_OGL;

   if (clip->xy_test_enable)
      dw2 |= GEN6_CLIP_DW2_XY_TEST_ENABLE;

   if (ilo_dev_gen(dev) < ILO_GEN(8) && clip->z_near_enable)
      dw2 |= GEN6_CLIP_DW2_Z_TEST_ENABLE;

   if (clip->gb_test_enable)
      dw2 |= GEN6_CLIP_DW2_GB_TEST_ENABLE;

   if (scan->barycentric_interps & (GEN6_INTERP_NONPERSPECTIVE_PIXEL |
                                    GEN6_INTERP_NONPERSPECTIVE_CENTROID |
                                    GEN6_INTERP_NONPERSPECTIVE_SAMPLE))
      dw2 |= GEN6_CLIP_DW2_NONPERSPECTIVE_BARYCENTRIC_ENABLE;

   if (setup->first_vertex_provoking) {
      dw2 |= 0 << GEN6_CLIP_DW2_TRI_PROVOKE__SHIFT |
             0 << GEN6_CLIP_DW2_LINE_PROVOKE__SHIFT |
             1 << GEN6_CLIP_DW2_TRIFAN_PROVOKE__SHIFT;
   } else {
      dw2 |= 2 << GEN6_CLIP_DW2_TRI_PROVOKE__SHIFT |
             1 << GEN6_CLIP_DW2_LINE_PROVOKE__SHIFT |
             2 << GEN6_CLIP_DW2_TRIFAN_PROVOKE__SHIFT;
   }

   dw3 = 0x1 << GEN6_CLIP_DW3_MIN_POINT_WIDTH__SHIFT |
         0x7ff << GEN6_CLIP_DW3_MAX_POINT_WIDTH__SHIFT |
         (clip->viewport_count - 1) << GEN6_CLIP_DW3_MAX_VPINDEX__SHIFT;

   if (clip->force_rtaindex_zero)
      dw3 |= GEN6_CLIP_DW3_FORCE_RTAINDEX_ZERO;

   STATIC_ASSERT(ARRAY_SIZE(rs->clip) >= 3);
   rs->clip[0] = dw1;
   rs->clip[1] = dw2;
   rs->clip[2] = dw3;

   return true;
}

static bool
raster_params_is_gen6_line_aa_allowed(const struct ilo_dev *dev,
                                      const struct ilo_state_raster_params_info *params)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 251:
    *
    *     "This field (Anti-aliasing Enable) must be disabled if any of the
    *      render targets have integer (UINT or SINT) surface format."
    */
   if (params->any_integer_rt)
      return false;

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 321:
    *
    *     "[DevSNB+]: This field (Hierarchical Depth Buffer Enable) must be
    *      disabled if Anti-aliasing Enable in 3DSTATE_SF is enabled.
    */
   if (ilo_dev_gen(dev) == ILO_GEN(6) && params->hiz_enable)
      return false;

   return true;
}

static void
raster_get_gen6_effective_line(const struct ilo_dev *dev,
                               const struct ilo_state_raster_info *info,
                               struct ilo_state_raster_line_info *line)
{
   const struct ilo_state_raster_setup_info *setup = &info->setup;
   const struct ilo_state_raster_params_info *params = &info->params;

   *line = info->line;

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 251:
    *
    *     "This field (Anti-aliasing Enable) is ignored when Multisample
    *      Rasterization Mode is MSRASTMODE_ON_xx."
    *
    * From the Sandy Bridge PRM, volume 2 part 1, page 251:
    *
    *     "Setting a Line Width of 0.0 specifies the rasterization of the
    *      "thinnest" (one-pixel-wide), non-antialiased lines. Note that
    *      this effectively overrides the effect of AAEnable (though the
    *      AAEnable state variable is not modified). Lines rendered with
    *      zero Line Width are rasterized using GIQ (Grid Intersection
    *      Quantization) rules as specified by the GDI and Direct3D APIs."
    *
    *     "Software must not program a value of 0.0 when running in
    *      MSRASTMODE_ON_xxx modes - zero-width lines are not available
    *      when multisampling rasterization is enabled."
    *
    * From the Sandy Bridge PRM, volume 2 part 1, page 294:
    *
    *     "Line stipple, controlled via the Line Stipple Enable state variable
    *      in WM_STATE, discards certain pixels that are produced by non-AA
    *      line rasterization."
    */
   if (setup->line_msaa_enable ||
       !raster_params_is_gen6_line_aa_allowed(dev, params))
      line->aa_enable = false;
   if (setup->line_msaa_enable || line->aa_enable) {
      line->stipple_enable = false;
      line->giq_enable = false;
      line->giq_last_pixel = false;
   }
}

static bool
raster_validate_gen8_raster(const struct ilo_dev *dev,
                            const struct ilo_state_raster_info *info)
{
   const struct ilo_state_raster_setup_info *setup = &info->setup;
   const struct ilo_state_raster_tri_info *tri = &info->tri;

   ILO_DEV_ASSERT(dev, 6, 8);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 249:
    *
    *     "This setting (SOLID) is required when rendering rectangle
    *      (RECTLIST) objects.
    */
   if (tri->fill_mode_front != GEN6_FILLMODE_SOLID ||
       tri->fill_mode_back != GEN6_FILLMODE_SOLID)
      assert(!setup->cv_is_rectangle);

   return true;
}

static enum gen_msrast_mode
raster_setup_get_gen6_msrast_mode(const struct ilo_dev *dev,
                                  const struct ilo_state_raster_setup_info *setup)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   if (setup->line_msaa_enable) {
      return (setup->msaa_enable) ? GEN6_MSRASTMODE_ON_PATTERN :
                                    GEN6_MSRASTMODE_ON_PIXEL;
   } else {
      return (setup->msaa_enable) ? GEN6_MSRASTMODE_OFF_PATTERN :
                                    GEN6_MSRASTMODE_OFF_PIXEL;
   }
}

static int
get_gen6_line_width(const struct ilo_dev *dev, float fwidth,
                    bool line_aa_enable, bool line_giq_enable)
{
   int line_width;

   ILO_DEV_ASSERT(dev, 6, 8);

   /* in U3.7 */
   line_width = (int) (fwidth * 128.0f + 0.5f);

   /*
    * Smooth lines should intersect ceil(line_width) or (ceil(line_width) + 1)
    * pixels in the minor direction.  We have to make the lines slightly
    * thicker, 0.5 pixel on both sides, so that they intersect that many
    * pixels.
    */
   if (line_aa_enable)
      line_width += 128;

   line_width = CLAMP(line_width, 1, 1023);

   if (line_giq_enable && line_width == 128)
      line_width = 0;

   return line_width;
}

static int
get_gen6_point_width(const struct ilo_dev *dev, float fwidth)
{
   int point_width;

   ILO_DEV_ASSERT(dev, 6, 8);

   /* in U8.3 */
   point_width = (int) (fwidth * 8.0f + 0.5f);
   point_width = CLAMP(point_width, 1, 2047);

   return point_width;
}

static bool
raster_set_gen7_3DSTATE_SF(struct ilo_state_raster *rs,
                           const struct ilo_dev *dev,
                           const struct ilo_state_raster_info *info,
                           const struct ilo_state_raster_line_info *line)
{
   const struct ilo_state_raster_clip_info *clip = &info->clip;
   const struct ilo_state_raster_setup_info *setup = &info->setup;
   const struct ilo_state_raster_point_info *point = &info->point;
   const struct ilo_state_raster_tri_info *tri = &info->tri;
   const struct ilo_state_raster_params_info *params = &info->params;
   const enum gen_msrast_mode msrast =
      raster_setup_get_gen6_msrast_mode(dev, setup);
   const int line_width = get_gen6_line_width(dev, params->line_width,
         line->aa_enable, line->giq_enable);
   const int point_width = get_gen6_point_width(dev, params->point_width);
   uint32_t dw1, dw2, dw3;

   ILO_DEV_ASSERT(dev, 6, 7.5);

   if (!raster_validate_gen8_raster(dev, info))
      return false;

   dw1 = tri->fill_mode_front << GEN7_SF_DW1_FILL_MODE_FRONT__SHIFT |
         tri->fill_mode_back << GEN7_SF_DW1_FILL_MODE_BACK__SHIFT |
         tri->front_winding << GEN7_SF_DW1_FRONT_WINDING__SHIFT;

   if (ilo_dev_gen(dev) >= ILO_GEN(7) && ilo_dev_gen(dev) <= ILO_GEN(7.5)) {
      enum gen_depth_format format;

      /* do it here as we want 0x0 to be valid */
      switch (tri->depth_offset_format) {
      case GEN6_ZFORMAT_D32_FLOAT_S8X24_UINT:
         format = GEN6_ZFORMAT_D32_FLOAT;
         break;
      case GEN6_ZFORMAT_D24_UNORM_S8_UINT:
         format = GEN6_ZFORMAT_D24_UNORM_X8_UINT;
         break;
      default:
         format = tri->depth_offset_format;
         break;
      }

      dw1 |= format << GEN7_SF_DW1_DEPTH_FORMAT__SHIFT;
   }

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 248:
    *
    *     "This bit (Statistics Enable) should be set whenever clipping is
    *      enabled and the Statistics Enable bit is set in CLIP_STATE. It
    *      should be cleared if clipping is disabled or Statistics Enable in
    *      CLIP_STATE is clear."
    */
   if (clip->stats_enable && clip->clip_enable)
      dw1 |= GEN7_SF_DW1_STATISTICS;

   /*
    * From the Ivy Bridge PRM, volume 2 part 1, page 258:
    *
    *     "This bit (Legacy Global Depth Bias Enable, Global Depth Offset
    *      Enable Solid , Global Depth Offset Enable Wireframe, and Global
    *      Depth Offset Enable Point) should be set whenever non zero depth
    *      bias (Slope, Bias) values are used. Setting this bit may have some
    *      degradation of performance for some workloads."
    *
    * But it seems fine to ignore that.
    */
   if (tri->depth_offset_solid)
      dw1 |= GEN7_SF_DW1_DEPTH_OFFSET_SOLID;
   if (tri->depth_offset_wireframe)
      dw1 |= GEN7_SF_DW1_DEPTH_OFFSET_WIREFRAME;
   if (tri->depth_offset_point)
      dw1 |= GEN7_SF_DW1_DEPTH_OFFSET_POINT;

   if (setup->viewport_transform)
      dw1 |= GEN7_SF_DW1_VIEWPORT_TRANSFORM;

   dw2 = tri->cull_mode << GEN7_SF_DW2_CULL_MODE__SHIFT |
         line_width << GEN7_SF_DW2_LINE_WIDTH__SHIFT |
         GEN7_SF_DW2_AA_LINE_CAP_1_0 |
         msrast << GEN7_SF_DW2_MSRASTMODE__SHIFT;

   if (line->aa_enable)
      dw2 |= GEN7_SF_DW2_AA_LINE_ENABLE;

   if (ilo_dev_gen(dev) == ILO_GEN(7.5) && line->stipple_enable)
      dw2 |= GEN75_SF_DW2_LINE_STIPPLE_ENABLE;

   if (setup->scissor_enable)
      dw2 |= GEN7_SF_DW2_SCISSOR_ENABLE;

   dw3 = GEN7_SF_DW3_TRUE_AA_LINE_DISTANCE |
         GEN7_SF_DW3_SUBPIXEL_8BITS;

   /* this has no effect when line_width != 0 */
   if (line->giq_last_pixel)
      dw3 |= GEN7_SF_DW3_LINE_LAST_PIXEL_ENABLE;

   if (setup->first_vertex_provoking) {
      dw3 |= 0 << GEN7_SF_DW3_TRI_PROVOKE__SHIFT |
             0 << GEN7_SF_DW3_LINE_PROVOKE__SHIFT |
             1 << GEN7_SF_DW3_TRIFAN_PROVOKE__SHIFT;
   } else {
      dw3 |= 2 << GEN7_SF_DW3_TRI_PROVOKE__SHIFT |
             1 << GEN7_SF_DW3_LINE_PROVOKE__SHIFT |
             2 << GEN7_SF_DW3_TRIFAN_PROVOKE__SHIFT;
   }

   /* setup->point_aa_enable is ignored */
   if (!point->programmable_width) {
      dw3 |= GEN7_SF_DW3_USE_POINT_WIDTH |
             point_width << GEN7_SF_DW3_POINT_WIDTH__SHIFT;
   }

   STATIC_ASSERT(ARRAY_SIZE(rs->sf) >= 3);
   rs->sf[0] = dw1;
   rs->sf[1] = dw2;
   rs->sf[2] = dw3;

   STATIC_ASSERT(ARRAY_SIZE(rs->raster) >= 4);
   rs->raster[0] = 0;
   rs->raster[1] = fui(params->depth_offset_const);
   rs->raster[2] = fui(params->depth_offset_scale);
   rs->raster[3] = fui(params->depth_offset_clamp);

   rs->line_aa_enable = line->aa_enable;
   rs->line_giq_enable = line->giq_enable;

   return true;
}

static bool
raster_set_gen8_3DSTATE_SF(struct ilo_state_raster *rs,
                           const struct ilo_dev *dev,
                           const struct ilo_state_raster_info *info,
                           const struct ilo_state_raster_line_info *line)
{
   const struct ilo_state_raster_clip_info *clip = &info->clip;
   const struct ilo_state_raster_setup_info *setup = &info->setup;
   const struct ilo_state_raster_point_info *point = &info->point;
   const struct ilo_state_raster_params_info *params = &info->params;
   const int line_width = get_gen6_line_width(dev, params->line_width,
         line->aa_enable, line->giq_enable);
   const int point_width = get_gen6_point_width(dev, params->point_width);
   uint32_t dw1, dw2, dw3;

   ILO_DEV_ASSERT(dev, 8, 8);

   dw1 = 0;

   if (clip->stats_enable && clip->clip_enable)
      dw1 |= GEN7_SF_DW1_STATISTICS;

   if (setup->viewport_transform)
      dw1 |= GEN7_SF_DW1_VIEWPORT_TRANSFORM;

   dw2 = line_width << GEN7_SF_DW2_LINE_WIDTH__SHIFT |
         GEN7_SF_DW2_AA_LINE_CAP_1_0;

   dw3 = GEN7_SF_DW3_TRUE_AA_LINE_DISTANCE |
         GEN7_SF_DW3_SUBPIXEL_8BITS;

   /* this has no effect when line_width != 0 */
   if (line->giq_last_pixel)
      dw3 |= GEN7_SF_DW3_LINE_LAST_PIXEL_ENABLE;

   if (setup->first_vertex_provoking) {
      dw3 |= 0 << GEN7_SF_DW3_TRI_PROVOKE__SHIFT |
             0 << GEN7_SF_DW3_LINE_PROVOKE__SHIFT |
             1 << GEN7_SF_DW3_TRIFAN_PROVOKE__SHIFT;
   } else {
      dw3 |= 2 << GEN7_SF_DW3_TRI_PROVOKE__SHIFT |
             1 << GEN7_SF_DW3_LINE_PROVOKE__SHIFT |
             2 << GEN7_SF_DW3_TRIFAN_PROVOKE__SHIFT;
   }

   if (!point->programmable_width) {
      dw3 |= GEN7_SF_DW3_USE_POINT_WIDTH |
             point_width << GEN7_SF_DW3_POINT_WIDTH__SHIFT;
   }

   STATIC_ASSERT(ARRAY_SIZE(rs->sf) >= 3);
   rs->sf[0] = dw1;
   rs->sf[1] = dw2;
   rs->sf[2] = dw3;

   return true;
}

static bool
raster_set_gen8_3DSTATE_RASTER(struct ilo_state_raster *rs,
                               const struct ilo_dev *dev,
                               const struct ilo_state_raster_info *info,
                               const struct ilo_state_raster_line_info *line)
{
   const struct ilo_state_raster_clip_info *clip = &info->clip;
   const struct ilo_state_raster_setup_info *setup = &info->setup;
   const struct ilo_state_raster_point_info *point = &info->point;
   const struct ilo_state_raster_tri_info *tri = &info->tri;
   const struct ilo_state_raster_params_info *params = &info->params;
   uint32_t dw1;

   ILO_DEV_ASSERT(dev, 8, 8);

   if (!raster_validate_gen8_raster(dev, info))
      return false;

   dw1 = tri->front_winding << GEN8_RASTER_DW1_FRONT_WINDING__SHIFT |
         tri->cull_mode << GEN8_RASTER_DW1_CULL_MODE__SHIFT |
         tri->fill_mode_front << GEN8_RASTER_DW1_FILL_MODE_FRONT__SHIFT |
         tri->fill_mode_back << GEN8_RASTER_DW1_FILL_MODE_BACK__SHIFT;

   if (point->aa_enable)
      dw1 |= GEN8_RASTER_DW1_SMOOTH_POINT_ENABLE;

   /* where should line_msaa_enable be set? */
   if (setup->msaa_enable)
      dw1 |= GEN8_RASTER_DW1_API_MULTISAMPLE_ENABLE;

   if (tri->depth_offset_solid)
      dw1 |= GEN8_RASTER_DW1_DEPTH_OFFSET_SOLID;
   if (tri->depth_offset_wireframe)
      dw1 |= GEN8_RASTER_DW1_DEPTH_OFFSET_WIREFRAME;
   if (tri->depth_offset_point)
      dw1 |= GEN8_RASTER_DW1_DEPTH_OFFSET_POINT;

   if (line->aa_enable)
      dw1 |= GEN8_RASTER_DW1_AA_LINE_ENABLE;

   if (setup->scissor_enable)
      dw1 |= GEN8_RASTER_DW1_SCISSOR_ENABLE;

   if (ilo_dev_gen(dev) >= ILO_GEN(9)) {
      if (clip->z_far_enable)
         dw1 |= GEN9_RASTER_DW1_Z_TEST_FAR_ENABLE;
      if (clip->z_near_enable)
         dw1 |= GEN9_RASTER_DW1_Z_TEST_NEAR_ENABLE;
   } else {
      if (clip->z_near_enable)
         dw1 |= GEN8_RASTER_DW1_Z_TEST_ENABLE;
   }

   STATIC_ASSERT(ARRAY_SIZE(rs->raster) >= 4);
   rs->raster[0] = dw1;
   rs->raster[1] = fui(params->depth_offset_const);
   rs->raster[2] = fui(params->depth_offset_scale);
   rs->raster[3] = fui(params->depth_offset_clamp);

   rs->line_aa_enable = line->aa_enable;
   rs->line_giq_enable = line->giq_enable;

   return true;
}

static enum gen_sample_count
get_gen6_sample_count(const struct ilo_dev *dev, uint8_t sample_count)
{
   enum gen_sample_count c;
   int min_gen;

   ILO_DEV_ASSERT(dev, 6, 8);

   switch (sample_count) {
   case 1:
      c = GEN6_NUMSAMPLES_1;
      min_gen = ILO_GEN(6);
      break;
   case 2:
      c = GEN8_NUMSAMPLES_2;
      min_gen = ILO_GEN(8);
      break;
   case 4:
      c = GEN6_NUMSAMPLES_4;
      min_gen = ILO_GEN(6);
      break;
   case 8:
      c = GEN7_NUMSAMPLES_8;
      min_gen = ILO_GEN(7);
      break;
   case 16:
      c = GEN8_NUMSAMPLES_16;
      min_gen = ILO_GEN(8);
      break;
   default:
      assert(!"unexpected sample count");
      c = GEN6_NUMSAMPLES_1;
      break;
   }

   assert(ilo_dev_gen(dev) >= min_gen);

   return c;
}

static bool
raster_set_gen8_3DSTATE_MULTISAMPLE(struct ilo_state_raster *rs,
                                    const struct ilo_dev *dev,
                                    const struct ilo_state_raster_info *info)
{
   const struct ilo_state_raster_setup_info *setup = &info->setup;
   const struct ilo_state_raster_scan_info *scan = &info->scan;
   const enum gen_sample_count count =
      get_gen6_sample_count(dev, scan->sample_count);
   uint32_t dw1;

   ILO_DEV_ASSERT(dev, 6, 8);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 307:
    *
    *     "Setting Multisample Rasterization Mode to MSRASTMODE_xxx_PATTERN
    *      when Number of Multisamples == NUMSAMPLES_1 is UNDEFINED."
    */
   if (setup->msaa_enable)
      assert(scan->sample_count > 1);

   dw1 = scan->pixloc << GEN6_MULTISAMPLE_DW1_PIXEL_LOCATION__SHIFT |
         count << GEN6_MULTISAMPLE_DW1_NUM_SAMPLES__SHIFT;

   STATIC_ASSERT(ARRAY_SIZE(rs->sample) >= 1);
   rs->sample[0] = dw1;

   return true;
}

static bool
raster_set_gen6_3DSTATE_SAMPLE_MASK(struct ilo_state_raster *rs,
                                    const struct ilo_dev *dev,
                                    const struct ilo_state_raster_info *info)
{
   const struct ilo_state_raster_scan_info *scan = &info->scan;
   /*
    * From the Ivy Bridge PRM, volume 2 part 1, page 294:
    *
    *     "If Number of Multisamples is NUMSAMPLES_1, bits 7:1 of this field
    *      (Sample Mask) must be zero.
    *
    *      If Number of Multisamples is NUMSAMPLES_4, bits 7:4 of this field
    *      must be zero."
    */
   const uint32_t mask = (1 << scan->sample_count) - 1;
   uint32_t dw1;

   ILO_DEV_ASSERT(dev, 6, 8);

   dw1 = (scan->sample_mask & mask) << GEN6_SAMPLE_MASK_DW1_VAL__SHIFT;

   STATIC_ASSERT(ARRAY_SIZE(rs->sample) >= 2);
   rs->sample[1] = dw1;

   return true;
}

static bool
raster_validate_gen6_wm(const struct ilo_dev *dev,
                        const struct ilo_state_raster_info *info)
{
   const struct ilo_state_raster_scan_info *scan = &info->scan;

   ILO_DEV_ASSERT(dev, 6, 8);

   if (ilo_dev_gen(dev) == ILO_GEN(6))
      assert(scan->earlyz_control == GEN7_EDSC_NORMAL);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 272:
    *
    *     "This bit (Statistics Enable) must be disabled if either of these
    *      bits is set: Depth Buffer Clear , Hierarchical Depth Buffer Resolve
    *      Enable or Depth Buffer Resolve Enable."
    */
   if (scan->earlyz_op != ILO_STATE_RASTER_EARLYZ_NORMAL)
      assert(!scan->stats_enable);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 273:
    *
    *     "If this field (Depth Buffer Resolve Enable) is enabled, the Depth
    *      Buffer Clear and Hierarchical Depth Buffer Resolve Enable fields
    *      must both be disabled."
    *
    *     "If this field (Hierarchical Depth Buffer Resolve Enable) is
    *      enabled, the Depth Buffer Clear and Depth Buffer Resolve Enable
    *      fields must both be disabled."
    *
    * This is guaranteed.
    */

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 314-315:
    *
    *     "Stencil buffer clear can be performed at the same time by enabling
    *      Stencil Buffer Write Enable."
    *
    *     "Note also that stencil buffer clear can be performed without depth
    *      buffer clear."
    */
   if (scan->earlyz_stencil_clear) {
      assert(scan->earlyz_op == ILO_STATE_RASTER_EARLYZ_NORMAL ||
             scan->earlyz_op == ILO_STATE_RASTER_EARLYZ_DEPTH_CLEAR);
   }

   return true;
}

static bool
raster_set_gen6_3dstate_wm(struct ilo_state_raster *rs,
                           const struct ilo_dev *dev,
                           const struct ilo_state_raster_info *info,
                           const struct ilo_state_raster_line_info *line)
{
   const struct ilo_state_raster_tri_info *tri = &info->tri;
   const struct ilo_state_raster_setup_info *setup = &info->setup;
   const struct ilo_state_raster_scan_info *scan = &info->scan;
   const enum gen_msrast_mode msrast =
      raster_setup_get_gen6_msrast_mode(dev, setup);
   /* only scan conversion states are set, as in Gen8+ */
   uint32_t dw4, dw5, dw6;

   ILO_DEV_ASSERT(dev, 6, 6);

   if (!raster_validate_gen6_wm(dev, info))
      return false;

   dw4 = 0;

   if (scan->stats_enable)
      dw4 |= GEN6_WM_DW4_STATISTICS;

   switch (scan->earlyz_op) {
   case ILO_STATE_RASTER_EARLYZ_DEPTH_CLEAR:
      dw4 |= GEN6_WM_DW4_DEPTH_CLEAR;
      break;
   case ILO_STATE_RASTER_EARLYZ_DEPTH_RESOLVE:
      dw4 |= GEN6_WM_DW4_DEPTH_RESOLVE;
      break;
   case ILO_STATE_RASTER_EARLYZ_HIZ_RESOLVE:
      dw4 |= GEN6_WM_DW4_HIZ_RESOLVE;
      break;
   default:
      if (scan->earlyz_stencil_clear)
         dw4 |= GEN6_WM_DW4_DEPTH_CLEAR;
      break;
   }

   dw5 = GEN6_WM_DW5_AA_LINE_CAP_1_0 | /* same as in 3DSTATE_SF */
         GEN6_WM_DW5_AA_LINE_WIDTH_2_0;

   if (tri->poly_stipple_enable)
      dw5 |= GEN6_WM_DW5_POLY_STIPPLE_ENABLE;
   if (line->stipple_enable)
      dw5 |= GEN6_WM_DW5_LINE_STIPPLE_ENABLE;

   dw6 = scan->zw_interp << GEN6_WM_DW6_ZW_INTERP__SHIFT |
         scan->barycentric_interps << GEN6_WM_DW6_BARYCENTRIC_INTERP__SHIFT |
         GEN6_WM_DW6_POINT_RASTRULE_UPPER_RIGHT |
         msrast << GEN6_WM_DW6_MSRASTMODE__SHIFT;

   STATIC_ASSERT(ARRAY_SIZE(rs->wm) >= 3);
   rs->wm[0] = dw4;
   rs->wm[1] = dw5;
   rs->wm[2] = dw6;

   return true;
}

static bool
raster_set_gen8_3DSTATE_WM(struct ilo_state_raster *rs,
                           const struct ilo_dev *dev,
                           const struct ilo_state_raster_info *info,
                           const struct ilo_state_raster_line_info *line)
{
   const struct ilo_state_raster_tri_info *tri = &info->tri;
   const struct ilo_state_raster_setup_info *setup = &info->setup;
   const struct ilo_state_raster_scan_info *scan = &info->scan;
   const enum gen_msrast_mode msrast =
      raster_setup_get_gen6_msrast_mode(dev, setup);
   uint32_t dw1;

   ILO_DEV_ASSERT(dev, 7, 8);

   if (!raster_validate_gen6_wm(dev, info))
      return false;

   dw1 = scan->earlyz_control << GEN7_WM_DW1_EDSC__SHIFT |
         scan->zw_interp << GEN7_WM_DW1_ZW_INTERP__SHIFT |
         scan->barycentric_interps << GEN7_WM_DW1_BARYCENTRIC_INTERP__SHIFT |
         GEN7_WM_DW1_AA_LINE_CAP_1_0 | /* same as in 3DSTATE_SF */
         GEN7_WM_DW1_AA_LINE_WIDTH_2_0 |
         GEN7_WM_DW1_POINT_RASTRULE_UPPER_RIGHT;

   if (scan->stats_enable)
      dw1 |= GEN7_WM_DW1_STATISTICS;

   if (ilo_dev_gen(dev) < ILO_GEN(8)) {
      switch (scan->earlyz_op) {
      case ILO_STATE_RASTER_EARLYZ_DEPTH_CLEAR:
         dw1 |= GEN7_WM_DW1_DEPTH_CLEAR;
         break;
      case ILO_STATE_RASTER_EARLYZ_DEPTH_RESOLVE:
         dw1 |= GEN7_WM_DW1_DEPTH_RESOLVE;
         break;
      case ILO_STATE_RASTER_EARLYZ_HIZ_RESOLVE:
         dw1 |= GEN7_WM_DW1_HIZ_RESOLVE;
         break;
      default:
         if (scan->earlyz_stencil_clear)
            dw1 |= GEN7_WM_DW1_DEPTH_CLEAR;
         break;
      }
   }

   if (tri->poly_stipple_enable)
      dw1 |= GEN7_WM_DW1_POLY_STIPPLE_ENABLE;
   if (line->stipple_enable)
      dw1 |= GEN7_WM_DW1_LINE_STIPPLE_ENABLE;

   if (ilo_dev_gen(dev) < ILO_GEN(8))
      dw1 |= msrast << GEN7_WM_DW1_MSRASTMODE__SHIFT;

   STATIC_ASSERT(ARRAY_SIZE(rs->wm) >= 1);
   rs->wm[0] = dw1;

   return true;
}

static bool
raster_set_gen8_3dstate_wm_hz_op(struct ilo_state_raster *rs,
                                 const struct ilo_dev *dev,
                                 const struct ilo_state_raster_info *info)
{
   const struct ilo_state_raster_scan_info *scan = &info->scan;
   const enum gen_sample_count count =
      get_gen6_sample_count(dev, scan->sample_count);
   const uint32_t mask = (1 << scan->sample_count) - 1;
   uint32_t dw1, dw4;

   ILO_DEV_ASSERT(dev, 8, 8);

   dw1 = count << GEN8_WM_HZ_DW1_NUM_SAMPLES__SHIFT;

   if (scan->earlyz_stencil_clear)
      dw1 |= GEN8_WM_HZ_DW1_STENCIL_CLEAR;

   switch (scan->earlyz_op) {
   case ILO_STATE_RASTER_EARLYZ_DEPTH_CLEAR:
      dw1 |= GEN8_WM_HZ_DW1_DEPTH_CLEAR;
      break;
   case ILO_STATE_RASTER_EARLYZ_DEPTH_RESOLVE:
      dw1 |= GEN8_WM_HZ_DW1_DEPTH_RESOLVE;
      break;
   case ILO_STATE_RASTER_EARLYZ_HIZ_RESOLVE:
      dw1 |= GEN8_WM_HZ_DW1_HIZ_RESOLVE;
      break;
   default:
      break;
   }

   dw4 = (scan->sample_mask & mask) << GEN8_WM_HZ_DW4_SAMPLE_MASK__SHIFT;

   STATIC_ASSERT(ARRAY_SIZE(rs->wm) >= 3);
   rs->wm[1] = dw1;
   rs->wm[2] = dw4;

   return true;
}

static bool
sample_pattern_get_gen6_packed_offsets(const struct ilo_dev *dev,
                                       uint8_t sample_count,
                                       const struct ilo_state_sample_pattern_offset_info *in,
                                       uint8_t *out)
{
   uint8_t max_dist, i;

   ILO_DEV_ASSERT(dev, 6, 8);

   max_dist = 0;
   for (i = 0; i < sample_count; i++) {
      const int8_t dist_x = (int8_t) in[i].x - 8;
      const int8_t dist_y = (int8_t) in[i].y - 8;
      const uint8_t dist = dist_x * dist_x + dist_y * dist_y;

      /*
       * From the Sandy Bridge PRM, volume 2 part 1, page 305:
       *
       *     "Programming Note: When programming the sample offsets (for
       *      NUMSAMPLES_4 or _8 and MSRASTMODE_xxx_PATTERN), the order of the
       *      samples 0 to 3 (or 7 for 8X) must have monotonically increasing
       *      distance from the pixel center. This is required to get the
       *      correct centroid computation in the device."
       */
      assert(dist >= max_dist);
      max_dist = dist;

      assert(in[i].x < 16);
      assert(in[i].y < 16);

      out[i] = in[i].x << 4 | in[i].y;
   }

   return true;
}

static bool
line_stipple_set_gen6_3DSTATE_LINE_STIPPLE(struct ilo_state_line_stipple *stipple,
                                           const struct ilo_dev *dev,
                                           const struct ilo_state_line_stipple_info *info)
{
   uint32_t dw1, dw2;

   ILO_DEV_ASSERT(dev, 6, 8);

   assert(info->repeat_count >= 1 && info->repeat_count <= 256);

   dw1 = info->pattern;
   if (ilo_dev_gen(dev) >= ILO_GEN(7)) {
      /* in U1.16 */
      const uint32_t inverse = 65536 / info->repeat_count;
      dw2 = inverse << GEN7_LINE_STIPPLE_DW2_INVERSE_REPEAT_COUNT__SHIFT |
            info->repeat_count << GEN6_LINE_STIPPLE_DW2_REPEAT_COUNT__SHIFT;
   } else {
      /* in U1.13 */
      const uint16_t inverse = 8192 / info->repeat_count;
      dw2 = inverse << GEN6_LINE_STIPPLE_DW2_INVERSE_REPEAT_COUNT__SHIFT |
            info->repeat_count << GEN6_LINE_STIPPLE_DW2_REPEAT_COUNT__SHIFT;
   }

   STATIC_ASSERT(ARRAY_SIZE(stipple->stipple) >= 2);
   stipple->stipple[0] = dw1;
   stipple->stipple[1] = dw2;

   return true;
}

static bool
sample_pattern_set_gen8_3DSTATE_SAMPLE_PATTERN(struct ilo_state_sample_pattern *pattern,
                                               const struct ilo_dev *dev,
                                               const struct ilo_state_sample_pattern_info *info)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   STATIC_ASSERT(ARRAY_SIZE(pattern->pattern_1x) >= 1);
   STATIC_ASSERT(ARRAY_SIZE(pattern->pattern_2x) >= 2);
   STATIC_ASSERT(ARRAY_SIZE(pattern->pattern_4x) >= 4);
   STATIC_ASSERT(ARRAY_SIZE(pattern->pattern_8x) >= 8);
   STATIC_ASSERT(ARRAY_SIZE(pattern->pattern_16x) >= 16);

   return (sample_pattern_get_gen6_packed_offsets(dev, 1,
              info->pattern_1x, pattern->pattern_1x) &&
           sample_pattern_get_gen6_packed_offsets(dev, 2,
              info->pattern_2x, pattern->pattern_2x) &&
           sample_pattern_get_gen6_packed_offsets(dev, 4,
              info->pattern_4x, pattern->pattern_4x) &&
           sample_pattern_get_gen6_packed_offsets(dev, 8,
              info->pattern_8x, pattern->pattern_8x) &&
           sample_pattern_get_gen6_packed_offsets(dev, 16,
              info->pattern_16x, pattern->pattern_16x));

}

static bool
poly_stipple_set_gen6_3DSTATE_POLY_STIPPLE_PATTERN(struct ilo_state_poly_stipple *stipple,
                                                   const struct ilo_dev *dev,
                                                   const struct ilo_state_poly_stipple_info *info)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   STATIC_ASSERT(ARRAY_SIZE(stipple->stipple) >= 32);
   memcpy(stipple->stipple, info->pattern, sizeof(info->pattern));

   return true;
}

bool
ilo_state_raster_init(struct ilo_state_raster *rs,
                      const struct ilo_dev *dev,
                      const struct ilo_state_raster_info *info)
{
   assert(ilo_is_zeroed(rs, sizeof(*rs)));
   return ilo_state_raster_set_info(rs, dev, info);
}

bool
ilo_state_raster_init_for_rectlist(struct ilo_state_raster *rs,
                                   const struct ilo_dev *dev,
                                   uint8_t sample_count,
                                   enum ilo_state_raster_earlyz_op earlyz_op,
                                   bool earlyz_stencil_clear)
{
   struct ilo_state_raster_info info;

   memset(&info, 0, sizeof(info));

   info.clip.viewport_count = 1;
   info.setup.cv_is_rectangle = true;
   info.setup.msaa_enable = (sample_count > 1);
   info.scan.sample_count = sample_count;
   info.scan.sample_mask = ~0u;
   info.scan.earlyz_op = earlyz_op;
   info.scan.earlyz_stencil_clear = earlyz_stencil_clear;

   return ilo_state_raster_init(rs, dev, &info);
}

bool
ilo_state_raster_set_info(struct ilo_state_raster *rs,
                          const struct ilo_dev *dev,
                          const struct ilo_state_raster_info *info)
{
   struct ilo_state_raster_line_info line;
   bool ret = true;

   ret &= raster_set_gen6_3DSTATE_CLIP(rs, dev, info);

   raster_get_gen6_effective_line(dev, info, &line);

   if (ilo_dev_gen(dev) >= ILO_GEN(8)) {
      ret &= raster_set_gen8_3DSTATE_SF(rs, dev, info, &line);
      ret &= raster_set_gen8_3DSTATE_RASTER(rs, dev, info, &line);
   } else {
      ret &= raster_set_gen7_3DSTATE_SF(rs, dev, info, &line);
   }

   ret &= raster_set_gen8_3DSTATE_MULTISAMPLE(rs, dev, info);
   ret &= raster_set_gen6_3DSTATE_SAMPLE_MASK(rs, dev, info);

   if (ilo_dev_gen(dev) >= ILO_GEN(7)) {
      ret &= raster_set_gen8_3DSTATE_WM(rs, dev, info, &line);

      if (ilo_dev_gen(dev) >= ILO_GEN(8))
         ret &= raster_set_gen8_3dstate_wm_hz_op(rs, dev, info);
   } else {
      ret &= raster_set_gen6_3dstate_wm(rs, dev, info, &line);
   }

   assert(ret);

   return ret;
}

bool
ilo_state_raster_set_params(struct ilo_state_raster *rs,
                            const struct ilo_dev *dev,
                            const struct ilo_state_raster_params_info *params)
{
   const bool line_aa_enable = (rs->line_aa_enable &&
         raster_params_is_gen6_line_aa_allowed(dev, params));
   const int line_width = get_gen6_line_width(dev, params->line_width,
         line_aa_enable, rs->line_giq_enable);

   ILO_DEV_ASSERT(dev, 6, 8);

   /* modify line AA enable */
   if (rs->line_aa_enable) {
      if (ilo_dev_gen(dev) >= ILO_GEN(8)) {
         if (line_aa_enable)
            rs->raster[0] |= GEN8_RASTER_DW1_AA_LINE_ENABLE;
         else
            rs->raster[0] &= ~GEN8_RASTER_DW1_AA_LINE_ENABLE;
      } else {
         if (line_aa_enable)
            rs->sf[1] |= GEN7_SF_DW2_AA_LINE_ENABLE;
         else
            rs->sf[1] &= ~GEN7_SF_DW2_AA_LINE_ENABLE;
      }
   }

   /* modify line width */
   rs->sf[1] = (rs->sf[1] & ~GEN7_SF_DW2_LINE_WIDTH__MASK) |
               line_width << GEN7_SF_DW2_LINE_WIDTH__SHIFT;

   /* modify point width */
   if (rs->sf[2] & GEN7_SF_DW3_USE_POINT_WIDTH) {
      const int point_width = get_gen6_point_width(dev, params->point_width);

      rs->sf[2] = (rs->sf[2] & ~GEN7_SF_DW3_POINT_WIDTH__MASK) |
                  point_width << GEN7_SF_DW3_POINT_WIDTH__SHIFT;
   }

   /* modify depth offset */
   rs->raster[1] = fui(params->depth_offset_const);
   rs->raster[2] = fui(params->depth_offset_scale);
   rs->raster[3] = fui(params->depth_offset_clamp);

   return true;
}

void
ilo_state_raster_full_delta(const struct ilo_state_raster *rs,
                            const struct ilo_dev *dev,
                            struct ilo_state_raster_delta *delta)
{
   delta->dirty = ILO_STATE_RASTER_3DSTATE_CLIP |
                  ILO_STATE_RASTER_3DSTATE_SF |
                  ILO_STATE_RASTER_3DSTATE_MULTISAMPLE |
                  ILO_STATE_RASTER_3DSTATE_SAMPLE_MASK |
                  ILO_STATE_RASTER_3DSTATE_WM |
                  ILO_STATE_RASTER_3DSTATE_AA_LINE_PARAMETERS;

   if (ilo_dev_gen(dev) >= ILO_GEN(8)) {
      delta->dirty |= ILO_STATE_RASTER_3DSTATE_RASTER |
                      ILO_STATE_RASTER_3DSTATE_WM_HZ_OP;
   }
}

void
ilo_state_raster_get_delta(const struct ilo_state_raster *rs,
                           const struct ilo_dev *dev,
                           const struct ilo_state_raster *old,
                           struct ilo_state_raster_delta *delta)
{
   delta->dirty = 0;

   if (memcmp(rs->clip, old->clip, sizeof(rs->clip)))
      delta->dirty |= ILO_STATE_RASTER_3DSTATE_CLIP;

   if (memcmp(rs->sf, old->sf, sizeof(rs->sf)))
      delta->dirty |= ILO_STATE_RASTER_3DSTATE_SF;

   if (memcmp(rs->raster, old->raster, sizeof(rs->raster))) {
      if (ilo_dev_gen(dev) >= ILO_GEN(8))
         delta->dirty |= ILO_STATE_RASTER_3DSTATE_RASTER;
      else
         delta->dirty |= ILO_STATE_RASTER_3DSTATE_SF;
   }

   if (memcmp(rs->sample, old->sample, sizeof(rs->sample))) {
      delta->dirty |= ILO_STATE_RASTER_3DSTATE_MULTISAMPLE |
                      ILO_STATE_RASTER_3DSTATE_SAMPLE_MASK;
   }

   if (memcmp(rs->wm, old->wm, sizeof(rs->wm))) {
      delta->dirty |= ILO_STATE_RASTER_3DSTATE_WM;

      if (ilo_dev_gen(dev) >= ILO_GEN(8))
         delta->dirty |= ILO_STATE_RASTER_3DSTATE_WM_HZ_OP;
   }
}

bool
ilo_state_sample_pattern_init(struct ilo_state_sample_pattern *pattern,
                              const struct ilo_dev *dev,
                              const struct ilo_state_sample_pattern_info *info)
{
   bool ret = true;

   ret &= sample_pattern_set_gen8_3DSTATE_SAMPLE_PATTERN(pattern, dev, info);

   assert(ret);

   return ret;
}

bool
ilo_state_sample_pattern_init_default(struct ilo_state_sample_pattern *pattern,
                                      const struct ilo_dev *dev)
{
   static const struct ilo_state_sample_pattern_info default_info = {
      .pattern_1x = {
         {  8,  8 },
      },

      .pattern_2x = {
         {  4,  4 }, { 12, 12 },
      },

      .pattern_4x = {
         {  6,  2 }, { 14,  6 }, {  2, 10 }, { 10, 14 },
      },

      /* \see brw_multisample_positions_8x */
      .pattern_8x = {
         {  7,  9 }, {  9, 13 }, { 11,  3 }, { 13, 11 },
         {  1,  7 }, {  5,  1 }, { 15,  5 }, {  3, 15 },
      },

      .pattern_16x = {
         {  8, 10 }, { 11,  8 }, {  5,  6 }, {  6,  4 },
         { 12, 11 }, { 13,  9 }, { 14,  7 }, { 10,  2 },
         {  4, 13 }, {  3,  3 }, {  7,  1 }, { 15,  5 },
         {  1, 12 }, {  9,  0 }, {  2, 14 }, {  0, 15 },
      },
   };

   return ilo_state_sample_pattern_init(pattern, dev, &default_info);
}

const uint8_t *
ilo_state_sample_pattern_get_packed_offsets(const struct ilo_state_sample_pattern *pattern,
                                            const struct ilo_dev *dev,
                                            uint8_t sample_count)
{
   switch (sample_count) {
   case 1:  return pattern->pattern_1x;
   case 2:  return pattern->pattern_2x;
   case 4:  return pattern->pattern_4x;
   case 8:  return pattern->pattern_8x;
   case 16: return pattern->pattern_16x;
   default:
      assert(!"unknown sample count");
      return NULL;
   }
}

void
ilo_state_sample_pattern_get_offset(const struct ilo_state_sample_pattern *pattern,
                                    const struct ilo_dev *dev,
                                    uint8_t sample_count, uint8_t sample_index,
                                    uint8_t *x, uint8_t *y)
{
   const const uint8_t *packed =
      ilo_state_sample_pattern_get_packed_offsets(pattern, dev, sample_count);

   assert(sample_index < sample_count);

   *x = (packed[sample_index] >> 4) & 0xf;
   *y = packed[sample_index] & 0xf;
}

/**
 * No need to initialize first.
 */
bool
ilo_state_line_stipple_set_info(struct ilo_state_line_stipple *stipple,
                                const struct ilo_dev *dev,
                                const struct ilo_state_line_stipple_info *info)
{
   bool ret = true;

   ret &= line_stipple_set_gen6_3DSTATE_LINE_STIPPLE(stipple,
         dev, info);

   assert(ret);

   return ret;
}

/**
 * No need to initialize first.
 */
bool
ilo_state_poly_stipple_set_info(struct ilo_state_poly_stipple *stipple,
                                const struct ilo_dev *dev,
                                const struct ilo_state_poly_stipple_info *info)
{
   bool ret = true;

   ret &= poly_stipple_set_gen6_3DSTATE_POLY_STIPPLE_PATTERN(stipple,
         dev, info);

   assert(ret);

   return ret;
}
