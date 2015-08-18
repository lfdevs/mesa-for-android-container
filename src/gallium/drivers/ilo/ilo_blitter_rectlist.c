/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2014 LunarG, Inc.
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

#include "util/u_draw.h"
#include "util/u_pack_color.h"

#include "ilo_draw.h"
#include "ilo_state.h"
#include "ilo_blit.h"
#include "ilo_blitter.h"

/**
 * Set the states that are invariant between all ops.
 */
static bool
ilo_blitter_set_invariants(struct ilo_blitter *blitter)
{
   struct ilo_state_vf_element_info elem;

   if (blitter->initialized)
      return true;

   /* a rectangle has 3 vertices in a RECTLIST */
   blitter->draw_info.topology = GEN6_3DPRIM_RECTLIST;
   blitter->draw_info.vertex_count = 3;
   blitter->draw_info.instance_count = 1;

   memset(&elem, 0, sizeof(elem));
   /* only vertex X and Y */
   elem.format = GEN6_FORMAT_R32G32_FLOAT;
   elem.format_size = 8;
   elem.component_count = 2;

   ilo_state_vf_init_for_rectlist(&blitter->vf, blitter->ilo->dev,
         blitter->vf_data, sizeof(blitter->vf_data), &elem, 1);

   ilo_state_vs_init_disabled(&blitter->vs, blitter->ilo->dev);
   ilo_state_hs_init_disabled(&blitter->hs, blitter->ilo->dev);
   ilo_state_ds_init_disabled(&blitter->ds, blitter->ilo->dev);
   ilo_state_gs_init_disabled(&blitter->gs, blitter->ilo->dev);
   ilo_state_sol_init_disabled(&blitter->sol, blitter->ilo->dev, false);

   /**
    * From the Haswell PRM, volume 7, page 615:
    *
    *     "The clear value must be between the min and max depth values
    *      (inclusive) defined in the CC_VIEWPORT."
    *
    * Even though clipping and viewport transformation will be disabled, we
    * still need to set up the viewport states.
    */
   ilo_state_viewport_init_for_rectlist(&blitter->vp, blitter->ilo->dev,
         blitter->vp_data, sizeof(blitter->vp_data));

   ilo_state_sbe_init_for_rectlist(&blitter->sbe, blitter->ilo->dev, 0, 0);
   ilo_state_ps_init_disabled(&blitter->ps, blitter->ilo->dev);

   ilo_state_urb_init_for_rectlist(&blitter->urb, blitter->ilo->dev,
         ilo_state_vf_get_attr_count(&blitter->vf));

   blitter->initialized = true;

   return true;
}

static void
ilo_blitter_set_earlyz_op(struct ilo_blitter *blitter,
                          enum ilo_state_raster_earlyz_op op,
                          bool earlyz_stencil_clear)
{
   blitter->earlyz_op = op;
   blitter->earlyz_stencil_clear = earlyz_stencil_clear;
}

/**
 * Set the rectangle primitive.
 */
static void
ilo_blitter_set_rectlist(struct ilo_blitter *blitter,
                         unsigned x, unsigned y,
                         unsigned width, unsigned height)
{
   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 11:
    *
    *     "(RECTLIST) A list of independent rectangles, where only 3 vertices
    *      are provided per rectangle object, with the fourth vertex implied
    *      by the definition of a rectangle. V0=LowerRight, V1=LowerLeft,
    *      V2=UpperLeft. Implied V3 = V0- V1+V2."
    */
   blitter->vertices[0][0] = (float) (x + width);
   blitter->vertices[0][1] = (float) (y + height);
   blitter->vertices[1][0] = (float) x;
   blitter->vertices[1][1] = (float) (y + height);
   blitter->vertices[2][0] = (float) x;
   blitter->vertices[2][1] = (float) y;
}

static void
ilo_blitter_set_depth_clear_value(struct ilo_blitter *blitter,
                                  uint32_t depth)
{
   blitter->depth_clear_value = depth;
}

static void
ilo_blitter_set_cc(struct ilo_blitter *blitter,
                   const struct ilo_state_cc_info *info)
{
   memset(&blitter->cc, 0, sizeof(blitter->cc));
   ilo_state_cc_init(&blitter->cc, blitter->ilo->dev, info);
}

static void
ilo_blitter_set_fb_rs(struct ilo_blitter *blitter)
{
   memset(&blitter->fb.rs, 0, sizeof(blitter->fb.rs));
   ilo_state_raster_init_for_rectlist(&blitter->fb.rs, blitter->ilo->dev,
         blitter->fb.num_samples, blitter->earlyz_op,
         blitter->earlyz_stencil_clear);
}

static void
ilo_blitter_set_fb(struct ilo_blitter *blitter,
                   struct pipe_resource *res, unsigned level,
                   const struct ilo_surface_cso *cso)
{
   struct ilo_texture *tex = ilo_texture(res);

   blitter->fb.width = u_minify(tex->image.width0, level);
   blitter->fb.height = u_minify(tex->image.height0, level);

   blitter->fb.num_samples = res->nr_samples;
   if (!blitter->fb.num_samples)
      blitter->fb.num_samples = 1;

   memcpy(&blitter->fb.dst, cso, sizeof(*cso));

   ilo_blitter_set_fb_rs(blitter);
}

static void
ilo_blitter_set_fb_from_surface(struct ilo_blitter *blitter,
                                struct pipe_surface *surf)
{
   ilo_blitter_set_fb(blitter, surf->texture, surf->u.tex.level,
         (const struct ilo_surface_cso *) surf);
}

static void
ilo_blitter_set_fb_from_resource(struct ilo_blitter *blitter,
                                 struct pipe_resource *res,
                                 enum pipe_format format,
                                 unsigned level, unsigned slice)
{
   struct pipe_surface templ, *surf;

   memset(&templ, 0, sizeof(templ));
   templ.format = format;
   templ.u.tex.level = level;
   templ.u.tex.first_layer = slice;
   templ.u.tex.last_layer = slice;

   /* if we did not call create_surface(), it would never fail */
   surf = blitter->ilo->base.create_surface(&blitter->ilo->base, res, &templ);
   assert(surf);

   ilo_blitter_set_fb(blitter, res, level,
         (const struct ilo_surface_cso *) surf);

   pipe_surface_reference(&surf, NULL);
}

static void
ilo_blitter_set_uses(struct ilo_blitter *blitter, uint32_t uses)
{
   blitter->uses = uses;
}

static void
hiz_align_fb(struct ilo_blitter *blitter)
{
   unsigned align_w, align_h;

   switch (blitter->earlyz_op) {
   case ILO_STATE_RASTER_EARLYZ_DEPTH_CLEAR:
   case ILO_STATE_RASTER_EARLYZ_DEPTH_RESOLVE:
      break;
   default:
      return;
      break;
   }

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 313-314:
    *
    *     "A rectangle primitive representing the clear area is delivered. The
    *      primitive must adhere to the following restrictions on size:
    *
    *      - If Number of Multisamples is NUMSAMPLES_1, the rectangle must be
    *        aligned to an 8x4 pixel block relative to the upper left corner
    *        of the depth buffer, and contain an integer number of these pixel
    *        blocks, and all 8x4 pixels must be lit.
    *
    *      - If Number of Multisamples is NUMSAMPLES_4, the rectangle must be
    *        aligned to a 4x2 pixel block (8x4 sample block) relative to the
    *        upper left corner of the depth buffer, and contain an integer
    *        number of these pixel blocks, and all samples of the 4x2 pixels
    *        must be lit
    *
    *      - If Number of Multisamples is NUMSAMPLES_8, the rectangle must be
    *        aligned to a 2x2 pixel block (8x4 sample block) relative to the
    *        upper left corner of the depth buffer, and contain an integer
    *        number of these pixel blocks, and all samples of the 2x2 pixels
    *        must be list."
    *
    *     "The following is required when performing a depth buffer resolve:
    *
    *      - A rectangle primitive of the same size as the previous depth
    *        buffer clear operation must be delivered, and depth buffer state
    *        cannot have changed since the previous depth buffer clear
    *        operation."
    */
   switch (blitter->fb.num_samples) {
   case 1:
      align_w = 8;
      align_h = 4;
      break;
   case 2:
      align_w = 4;
      align_h = 4;
      break;
   case 4:
      align_w = 4;
      align_h = 2;
      break;
   case 8:
   default:
      align_w = 2;
      align_h = 2;
      break;
   }

   if (blitter->fb.width % align_w || blitter->fb.height % align_h) {
      blitter->fb.width = align(blitter->fb.width, align_w);
      blitter->fb.height = align(blitter->fb.height, align_h);
   }
}

static void
hiz_emit_rectlist(struct ilo_blitter *blitter)
{
   hiz_align_fb(blitter);

   ilo_blitter_set_rectlist(blitter, 0, 0,
         blitter->fb.width, blitter->fb.height);

   ilo_draw_rectlist(blitter->ilo);
}

static bool
hiz_can_clear_zs(const struct ilo_blitter *blitter,
                 const struct ilo_texture *tex)
{
   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 314:
    *
    *     "Several cases exist where Depth Buffer Clear cannot be enabled (the
    *      legacy method of clearing must be performed):
    *
    *      - If the depth buffer format is D32_FLOAT_S8X24_UINT or
    *        D24_UNORM_S8_UINT.
    *
    *      - If stencil test is enabled but the separate stencil buffer is
    *        disabled.
    *
    *      - [DevSNB-A{W/A}]: ...
    *
    *      - [DevSNB{W/A}]: When depth buffer format is D16_UNORM and the
    *        width of the map (LOD0) is not multiple of 16, fast clear
    *        optimization must be disabled."
    *
    * From the Ivy Bridge PRM, volume 2 part 1, page 313:
    *
    *     "Several cases exist where Depth Buffer Clear cannot be enabled (the
    *      legacy method of clearing must be performed):
    *
    *      - If the depth buffer format is D32_FLOAT_S8X24_UINT or
    *        D24_UNORM_S8_UINT.
    *
    *      - If stencil test is enabled but the separate stencil buffer is
    *        disabled."
    *
    * The truth is when HiZ is enabled, separate stencil is also enabled on
    * all GENs.  The depth buffer format cannot be combined depth/stencil.
    */
   switch (tex->image_format) {
   case PIPE_FORMAT_Z16_UNORM:
      if (ilo_dev_gen(blitter->ilo->dev) == ILO_GEN(6) &&
          tex->base.width0 % 16)
         return false;
      break;
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:
   case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
      assert(!"HiZ with combined depth/stencil");
      return false;
      break;
   default:
      break;
   }

   return true;
}

bool
ilo_blitter_rectlist_clear_zs(struct ilo_blitter *blitter,
                              struct pipe_surface *zs,
                              unsigned clear_flags,
                              double depth, unsigned stencil)
{
   struct ilo_texture *tex = ilo_texture(zs->texture);
   struct ilo_state_cc_info info;
   uint32_t uses, clear_value;

   if (!ilo_image_can_enable_aux(&tex->image, zs->u.tex.level))
      return false;

   if (!hiz_can_clear_zs(blitter, tex))
      return false;

   if (ilo_dev_gen(blitter->ilo->dev) >= ILO_GEN(8))
      clear_value = fui(depth);
   else
      clear_value = util_pack_z(tex->image_format, depth);

   ilo_blit_resolve_surface(blitter->ilo, zs,
         ILO_TEXTURE_RENDER_WRITE | ILO_TEXTURE_CLEAR);
   ilo_texture_set_slice_clear_value(tex, zs->u.tex.level,
         zs->u.tex.first_layer,
         zs->u.tex.last_layer - zs->u.tex.first_layer + 1,
         clear_value);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 313-314:
    *
    *     "- Depth Test Enable must be disabled and Depth Buffer Write Enable
    *        must be enabled (if depth is being cleared).
    *
    *      - Stencil buffer clear can be performed at the same time by
    *        enabling Stencil Buffer Write Enable.  Stencil Test Enable must
    *        be enabled and Stencil Pass Depth Pass Op set to REPLACE, and the
    *        clear value that is placed in the stencil buffer is the Stencil
    *        Reference Value from COLOR_CALC_STATE.
    *
    *      - Note also that stencil buffer clear can be performed without
    *        depth buffer clear. For stencil only clear, Depth Test Enable and
    *        Depth Buffer Write Enable must be disabled.
    *
    *      - [DevSNB] errata: For stencil buffer only clear, the previous
    *        depth clear value must be delivered during the clear."
    */
   memset(&info, 0, sizeof(info));

   if (clear_flags & PIPE_CLEAR_DEPTH) {
      info.depth.cv_has_buffer = true;
      info.depth.write_enable = true;
   }

   if (clear_flags & PIPE_CLEAR_STENCIL) {
      info.stencil.cv_has_buffer = true;
      info.stencil.test_enable = true;
      info.stencil.front.test_func = GEN6_COMPAREFUNCTION_ALWAYS;
      info.stencil.front.fail_op = GEN6_STENCILOP_KEEP;
      info.stencil.front.zfail_op = GEN6_STENCILOP_KEEP;
      info.stencil.front.zpass_op = GEN6_STENCILOP_REPLACE;

      /*
       * From the Ivy Bridge PRM, volume 2 part 1, page 277:
       *
       *     "Additionally the following must be set to the correct values.
       *
       *      - DEPTH_STENCIL_STATE::Stencil Write Mask must be 0xFF
       *      - DEPTH_STENCIL_STATE::Stencil Test Mask must be 0xFF
       *      - DEPTH_STENCIL_STATE::Back Face Stencil Write Mask must be 0xFF
       *      - DEPTH_STENCIL_STATE::Back Face Stencil Test Mask must be 0xFF"
       *
       * Back frace masks will be copied from front face masks.
       */
      info.params.stencil_front.test_ref = (uint8_t) stencil;
      info.params.stencil_front.test_mask = 0xff;
      info.params.stencil_front.write_mask = 0xff;
   }

   ilo_blitter_set_invariants(blitter);
   ilo_blitter_set_earlyz_op(blitter,
         ILO_STATE_RASTER_EARLYZ_DEPTH_CLEAR,
         clear_flags & PIPE_CLEAR_STENCIL);

   ilo_blitter_set_cc(blitter, &info);
   ilo_blitter_set_depth_clear_value(blitter, clear_value);
   ilo_blitter_set_fb_from_surface(blitter, zs);

   uses = ILO_BLITTER_USE_DSA;
   if (clear_flags & PIPE_CLEAR_DEPTH)
      uses |= ILO_BLITTER_USE_VIEWPORT | ILO_BLITTER_USE_FB_DEPTH;
   if (clear_flags & PIPE_CLEAR_STENCIL)
      uses |= ILO_BLITTER_USE_CC | ILO_BLITTER_USE_FB_STENCIL;
   ilo_blitter_set_uses(blitter, uses);

   hiz_emit_rectlist(blitter);

   return true;
}

void
ilo_blitter_rectlist_resolve_z(struct ilo_blitter *blitter,
                               struct pipe_resource *res,
                               unsigned level, unsigned slice)
{
   struct ilo_texture *tex = ilo_texture(res);
   struct ilo_state_cc_info info;
   const struct ilo_texture_slice *s =
      ilo_texture_get_slice(tex, level, slice);

   if (!ilo_image_can_enable_aux(&tex->image, level))
      return;

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 314:
    *
    *     "Depth Test Enable must be enabled with the Depth Test Function set
    *      to NEVER. Depth Buffer Write Enable must be enabled. Stencil Test
    *      Enable and Stencil Buffer Write Enable must be disabled."
    */
   memset(&info, 0, sizeof(info));
   info.depth.cv_has_buffer = true;
   info.depth.test_enable = true;
   info.depth.write_enable = true;
   info.depth.test_func = GEN6_COMPAREFUNCTION_NEVER;

   ilo_blitter_set_invariants(blitter);
   ilo_blitter_set_earlyz_op(blitter,
         ILO_STATE_RASTER_EARLYZ_DEPTH_RESOLVE, false);

   ilo_blitter_set_cc(blitter, &info);
   ilo_blitter_set_depth_clear_value(blitter, s->clear_value);
   ilo_blitter_set_fb_from_resource(blitter, res, res->format, level, slice);
   ilo_blitter_set_uses(blitter,
         ILO_BLITTER_USE_DSA | ILO_BLITTER_USE_FB_DEPTH);

   hiz_emit_rectlist(blitter);
}

void
ilo_blitter_rectlist_resolve_hiz(struct ilo_blitter *blitter,
                                 struct pipe_resource *res,
                                 unsigned level, unsigned slice)
{
   struct ilo_texture *tex = ilo_texture(res);
   struct ilo_state_cc_info info;

   if (!ilo_image_can_enable_aux(&tex->image, level))
      return;

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 315:
    *
    *     "(Hierarchical Depth Buffer Resolve) Depth Test Enable must be
    *      disabled. Depth Buffer Write Enable must be enabled. Stencil Test
    *      Enable and Stencil Buffer Write Enable must be disabled."
    */
   memset(&info, 0, sizeof(info));
   info.depth.cv_has_buffer = true;
   info.depth.write_enable = true;

   ilo_blitter_set_invariants(blitter);
   ilo_blitter_set_earlyz_op(blitter,
         ILO_STATE_RASTER_EARLYZ_HIZ_RESOLVE, false);

   ilo_blitter_set_cc(blitter, &info);
   ilo_blitter_set_fb_from_resource(blitter, res, res->format, level, slice);
   ilo_blitter_set_uses(blitter,
         ILO_BLITTER_USE_DSA | ILO_BLITTER_USE_FB_DEPTH);

   hiz_emit_rectlist(blitter);
}
