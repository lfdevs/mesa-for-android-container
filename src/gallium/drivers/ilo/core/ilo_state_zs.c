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
#include "ilo_image.h"
#include "ilo_vma.h"
#include "ilo_state_zs.h"

static bool
zs_set_gen6_null_3DSTATE_DEPTH_BUFFER(struct ilo_state_zs *zs,
                                      const struct ilo_dev *dev)
{
   const enum gen_depth_format format = GEN6_ZFORMAT_D32_FLOAT;
   uint32_t dw1;

   ILO_DEV_ASSERT(dev, 6, 8);

   if (ilo_dev_gen(dev) >= ILO_GEN(7)) {
      dw1 = GEN6_SURFTYPE_NULL << GEN7_DEPTH_DW1_TYPE__SHIFT |
            format << GEN7_DEPTH_DW1_FORMAT__SHIFT;
   } else {
      dw1 = GEN6_SURFTYPE_NULL << GEN6_DEPTH_DW1_TYPE__SHIFT |
            GEN6_TILING_Y << GEN6_DEPTH_DW1_TILING__SHIFT |
            format << GEN6_DEPTH_DW1_FORMAT__SHIFT;
   }

   STATIC_ASSERT(ARRAY_SIZE(zs->depth) >= 5);
   zs->depth[0] = dw1;
   zs->depth[1] = 0;
   zs->depth[2] = 0;
   zs->depth[3] = 0;
   zs->depth[4] = 0;

   return true;
}

static bool
zs_validate_gen6(const struct ilo_dev *dev,
                 const struct ilo_state_zs_info *info)
{
   const struct ilo_image *img = (info->z_img) ? info->z_img : info->s_img;

   ILO_DEV_ASSERT(dev, 6, 8);

   assert(!info->z_img == !info->z_vma);
   assert(!info->s_img == !info->s_vma);

   /* all tiled */
   if (info->z_img) {
      assert(info->z_img->tiling == GEN6_TILING_Y);
      assert(info->z_vma->vm_alignment % 4096 == 0);
   }
   if (info->s_img) {
      assert(info->s_img->tiling == GEN8_TILING_W);
      assert(info->s_vma->vm_alignment % 4096 == 0);
   }
   if (info->hiz_vma) {
      assert(info->z_img &&
             ilo_image_can_enable_aux(info->z_img, info->level));
      assert(info->z_vma->vm_alignment % 4096 == 0);
   }

   /*
    * From the Ivy Bridge PRM, volume 2 part 1, page 315:
    *
    *     "The stencil buffer has a format of S8_UINT, and shares Surface
    *      Type, Height, Width, and Depth, Minimum Array Element, Render
    *      Target View Extent, Depth Coordinate Offset X/Y, LOD, and Depth
    *      Buffer Object Control State fields of the depth buffer."
    */
   if (info->z_img && info->s_img && info->z_img != info->s_img) {
      assert(info->z_img->type == info->s_img->type &&
             info->z_img->height0 == info->s_img->height0 &&
             info->z_img->depth0 == info->s_img->depth0);
   }

   if (info->type != img->type) {
      assert(info->type == GEN6_SURFTYPE_2D &&
             img->type == GEN6_SURFTYPE_CUBE);
   }

   if (ilo_dev_gen(dev) >= ILO_GEN(7)) {
      switch (info->format) {
      case GEN6_ZFORMAT_D32_FLOAT:
      case GEN6_ZFORMAT_D24_UNORM_X8_UINT:
      case GEN6_ZFORMAT_D16_UNORM:
         break;
      default:
         assert(!"unknown depth format");
         break;
      }
   } else {
      /*
       * From the Ironlake PRM, volume 2 part 1, page 330:
       *
       *     "If this field (Separate Stencil Buffer Enable) is disabled, the
       *      Surface Format of the depth buffer cannot be D24_UNORM_X8_UINT."
       *
       * From the Sandy Bridge PRM, volume 2 part 1, page 321:
       *
       *     "[DevSNB]: This field (Separate Stencil Buffer Enable) must be
       *      set to the same value (enabled or disabled) as Hierarchical
       *      Depth Buffer Enable."
       */
      if (info->hiz_vma)
         assert(info->format != GEN6_ZFORMAT_D24_UNORM_S8_UINT);
      else
         assert(info->format != GEN6_ZFORMAT_D24_UNORM_X8_UINT);
   }

   assert(info->level < img->level_count);
   assert(img->bo_stride);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 323:
    *
    *     "For cube maps, Width must be set equal to Height."
    */
   if (info->type == GEN6_SURFTYPE_CUBE)
      assert(img->width0 == img->height0);

   return true;
}

static void
zs_get_gen6_max_extent(const struct ilo_dev *dev,
                       const struct ilo_state_zs_info *info,
                       uint16_t *max_w, uint16_t *max_h)
{
   const uint16_t max_size = (ilo_dev_gen(dev) >= ILO_GEN(7)) ? 16384 : 8192;

   ILO_DEV_ASSERT(dev, 6, 8);

   switch (info->type) {
   case GEN6_SURFTYPE_1D:
      *max_w = max_size;
      *max_h = 1;
      break;
   case GEN6_SURFTYPE_2D:
   case GEN6_SURFTYPE_CUBE:
      *max_w = max_size;
      *max_h = max_size;
      break;
   case GEN6_SURFTYPE_3D:
      *max_w = 2048;
      *max_h = 2048;
      break;
   default:
      assert(!"invalid surface type");
      *max_w = 1;
      *max_h = 1;
      break;
   }
}

static void
get_gen6_hiz_alignments(const struct ilo_dev *dev,
                        const struct ilo_image *img,
                        uint16_t *align_w, uint16_t *align_h)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 313:
    *
    *     "A rectangle primitive representing the clear area is delivered. The
    *      primitive must adhere to the following restrictions on size:
    *
    *      - If Number of Multisamples is NUMSAMPLES_1, the rectangle must be
    *        aligned to an 8x4 pixel block relative to the upper left corner
    *        of the depth buffer, and contain an integer number of these pixel
    *        blocks, and all 8x4 pixels must be lit.
    *      - If Number of Multisamples is NUMSAMPLES_4, the rectangle must be
    *        aligned to a 4x2 pixel block (8x4 sample block) relative to the
    *        upper left corner of the depth buffer, and contain an integer
    *        number of these pixel blocks, and all samples of the 4x2 pixels
    *        must be lit
    *      - If Number of Multisamples is NUMSAMPLES_8, the rectangle must be
    *        aligned to a 2x2 pixel block (8x4 sample block) relative to the
    *        upper left corner of the depth buffer, and contain an integer
    *        number of these pixel blocks, and all samples of the 2x2 pixels
    *        must be list."
    *
    * Experiments on Gen7.5 show that HiZ resolve also requires the rectangle
    * to be aligned to 8x4 sample blocks.  But to be on the safe side, we
    * always require a level to be aligned when HiZ is enabled.
    */
   switch (img->sample_count) {
   case 1:
      *align_w = 8;
      *align_h = 4;
      break;
   case 2:
      *align_w = 4;
      *align_h = 4;
      break;
   case 4:
      *align_w = 4;
      *align_h = 2;
      break;
   case 8:
      *align_w = 2;
      *align_h = 2;
      break;
   case 16:
      *align_w = 2;
      *align_h = 1;
      break;
   default:
      assert(!"unknown sample count");
      *align_w = 1;
      *align_h = 1;
      break;
   }
}

static bool
zs_get_gen6_depth_extent(const struct ilo_dev *dev,
                         const struct ilo_state_zs_info *info,
                         uint16_t *width, uint16_t *height)
{
   const struct ilo_image *img = (info->z_img) ? info->z_img : info->s_img;
   uint16_t w, h, max_w, max_h;

   ILO_DEV_ASSERT(dev, 6, 8);

   w = img->width0;
   h = img->height0;

   if (info->hiz_vma) {
      uint16_t align_w, align_h;

      get_gen6_hiz_alignments(dev, info->z_img, &align_w, &align_h);

      /*
       * We want to force 8x4 alignment, but we can do so only for level 0 and
       * only when it is padded.  ilo_image should know all these.
       */
      if (info->level)
         assert(w % align_w == 0 && h % align_h == 0);

      w = align(w, align_w);
      h = align(h, align_h);
   }

   zs_get_gen6_max_extent(dev, info, &max_w, &max_h);
   assert(w && h && w <= max_w && h <= max_h);

   *width = w - 1;
   *height = h - 1;

   return true;
}

static bool
zs_get_gen6_depth_slices(const struct ilo_dev *dev,
                         const struct ilo_state_zs_info *info,
                         uint16_t *depth, uint16_t *min_array_elem,
                         uint16_t *rt_view_extent)
{
   const struct ilo_image *img = (info->z_img) ? info->z_img : info->s_img;
   uint16_t max_slice, d;

   ILO_DEV_ASSERT(dev, 6, 8);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 325:
    *
    *     "This field (Depth) specifies the total number of levels for a
    *      volume texture or the number of array elements allowed to be
    *      accessed starting at the Minimum Array Element for arrayed
    *      surfaces. If the volume texture is MIP-mapped, this field specifies
    *      the depth of the base MIP level."
    */
   switch (info->type) {
   case GEN6_SURFTYPE_1D:
   case GEN6_SURFTYPE_2D:
   case GEN6_SURFTYPE_CUBE:
      max_slice = (ilo_dev_gen(dev) >= ILO_GEN(7)) ? 2048 : 512;

      assert(img->array_size <= max_slice);
      max_slice = img->array_size;

      d = info->slice_count;
      if (info->type == GEN6_SURFTYPE_CUBE) {
         /*
          * Minumum Array Element and Depth must be 0; Render Target View
          * Extent is ignored.
          */
         if (info->slice_base || d != 6) {
            ilo_warn("no cube array dpeth buffer\n");
            return false;
         }

         d /= 6;
      }
      break;
   case GEN6_SURFTYPE_3D:
      max_slice = 2048;

      assert(img->depth0 <= max_slice);
      max_slice = u_minify(img->depth0, info->level);

      d = img->depth0;
      break;
   default:
      assert(!"invalid surface type");
      return false;
      break;
   }

   if (!info->slice_count ||
       info->slice_base + info->slice_count > max_slice) {
      ilo_warn("invalid slice range\n");
      return false;
   }

   assert(d);
   *depth = d - 1;

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 325:
    *
    *     "For 1D and 2D Surfaces:
    *      This field (Minimum Array Element) indicates the minimum array
    *      element that can be accessed as part of this surface. The delivered
    *      array index is added to this field before being used to address the
    *      surface.
    *
    *      For 3D Surfaces:
    *      This field indicates the minimum `R' coordinate on the LOD
    *      currently being rendered to.  This field is added to the delivered
    *      array index before it is used to address the surface.
    *
    *      For Other Surfaces:
    *      This field is ignored."
    */
   *min_array_elem = info->slice_base;

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 326:
    *
    *     "For 3D Surfaces:
    *      This field (Render Target View Extent) indicates the extent of the
    *      accessible `R' coordinates minus 1 on the LOD currently being
    *      rendered to.
    *
    *      For 1D and 2D Surfaces:
    *      This field must be set to the same value as the Depth field.
    *
    *      For Other Surfaces:
    *      This field is ignored."
    */
   *rt_view_extent = info->slice_count - 1;

   return true;
}

static bool
zs_set_gen6_3DSTATE_DEPTH_BUFFER(struct ilo_state_zs *zs,
                                 const struct ilo_dev *dev,
                                 const struct ilo_state_zs_info *info)
{
   uint16_t width, height, depth, array_base, view_extent;
   uint32_t dw1, dw2, dw3, dw4;

   ILO_DEV_ASSERT(dev, 6, 6);

   if (!zs_validate_gen6(dev, info) ||
       !zs_get_gen6_depth_extent(dev, info, &width, &height) ||
       !zs_get_gen6_depth_slices(dev, info, &depth, &array_base,
                                 &view_extent))
      return false;

   /* info->z_readonly and info->s_readonly are ignored on Gen6 */
   dw1 = info->type << GEN6_DEPTH_DW1_TYPE__SHIFT |
         GEN6_TILING_Y << GEN6_DEPTH_DW1_TILING__SHIFT |
         info->format << GEN6_DEPTH_DW1_FORMAT__SHIFT;

   if (info->z_img)
      dw1 |= (info->z_img->bo_stride - 1) << GEN6_DEPTH_DW1_PITCH__SHIFT;

   if (info->hiz_vma || !info->z_img) {
      dw1 |= GEN6_DEPTH_DW1_HIZ_ENABLE |
             GEN6_DEPTH_DW1_SEPARATE_STENCIL;
   }

   dw2 = 0;
   dw3 = height << GEN6_DEPTH_DW3_HEIGHT__SHIFT |
         width << GEN6_DEPTH_DW3_WIDTH__SHIFT |
         info->level << GEN6_DEPTH_DW3_LOD__SHIFT |
         GEN6_DEPTH_DW3_MIPLAYOUT_BELOW;
   dw4 = depth << GEN6_DEPTH_DW4_DEPTH__SHIFT |
         array_base << GEN6_DEPTH_DW4_MIN_ARRAY_ELEMENT__SHIFT |
         view_extent << GEN6_DEPTH_DW4_RT_VIEW_EXTENT__SHIFT;

   STATIC_ASSERT(ARRAY_SIZE(zs->depth) >= 5);
   zs->depth[0] = dw1;
   zs->depth[1] = dw2;
   zs->depth[2] = dw3;
   zs->depth[3] = dw4;
   zs->depth[4] = 0;

   return true;
}

static bool
zs_set_gen7_3DSTATE_DEPTH_BUFFER(struct ilo_state_zs *zs,
                                 const struct ilo_dev *dev,
                                 const struct ilo_state_zs_info *info)
{
   uint16_t width, height, depth;
   uint16_t array_base, view_extent;
   uint32_t dw1, dw2, dw3, dw4, dw6;

   ILO_DEV_ASSERT(dev, 7, 8);

   if (!zs_validate_gen6(dev, info) ||
       !zs_get_gen6_depth_extent(dev, info, &width, &height) ||
       !zs_get_gen6_depth_slices(dev, info, &depth, &array_base,
                                 &view_extent))
      return false;

   dw1 = info->type << GEN7_DEPTH_DW1_TYPE__SHIFT |
         info->format << GEN7_DEPTH_DW1_FORMAT__SHIFT;

   if (info->z_img) {
      if (!info->z_readonly)
         dw1 |= GEN7_DEPTH_DW1_DEPTH_WRITE_ENABLE;
      if (info->hiz_vma)
         dw1 |= GEN7_DEPTH_DW1_HIZ_ENABLE;

      dw1 |= (info->z_img->bo_stride - 1) << GEN7_DEPTH_DW1_PITCH__SHIFT;
   }

   if (info->s_img && !info->s_readonly)
      dw1 |= GEN7_DEPTH_DW1_STENCIL_WRITE_ENABLE;

   dw2 = 0;
   dw3 = height << GEN7_DEPTH_DW3_HEIGHT__SHIFT |
         width << GEN7_DEPTH_DW3_WIDTH__SHIFT |
         info->level << GEN7_DEPTH_DW3_LOD__SHIFT;
   dw4 = depth << GEN7_DEPTH_DW4_DEPTH__SHIFT |
         array_base << GEN7_DEPTH_DW4_MIN_ARRAY_ELEMENT__SHIFT;
   dw6 = view_extent << GEN7_DEPTH_DW6_RT_VIEW_EXTENT__SHIFT;

   if (ilo_dev_gen(dev) >= ILO_GEN(8) && info->z_img) {
      assert(info->z_img->walk_layer_height % 4 == 0);
      /* note that DW is off-by-one for Gen8+ */
      dw6 |= (info->z_img->walk_layer_height / 4) <<
         GEN8_DEPTH_DW7_QPITCH__SHIFT;
   }

   STATIC_ASSERT(ARRAY_SIZE(zs->depth) >= 5);
   zs->depth[0] = dw1;
   zs->depth[1] = dw2;
   zs->depth[2] = dw3;
   zs->depth[3] = dw4;
   zs->depth[4] = dw6;

   return true;
}

static bool
zs_set_gen6_null_3DSTATE_STENCIL_BUFFER(struct ilo_state_zs *zs,
                                        const struct ilo_dev *dev)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   STATIC_ASSERT(ARRAY_SIZE(zs->stencil) >= 3);
   zs->stencil[0] = 0;
   zs->stencil[1] = 0;
   if (ilo_dev_gen(dev) >= ILO_GEN(8))
      zs->stencil[2] = 0;

   return true;
}

static bool
zs_set_gen6_3DSTATE_STENCIL_BUFFER(struct ilo_state_zs *zs,
                                   const struct ilo_dev *dev,
                                   const struct ilo_state_zs_info *info)
{
   const struct ilo_image *img = info->s_img;
   uint32_t dw1, dw2;

   ILO_DEV_ASSERT(dev, 6, 8);

   assert(img->bo_stride);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 329:
    *
    *     "The pitch must be set to 2x the value computed based on width, as
    *      the stencil buffer is stored with two rows interleaved."
    *
    * For Gen7+, we still dobule the stride because we did not double the
    * slice widths when initializing ilo_image.
    */
   dw1 = (img->bo_stride * 2 - 1) << GEN6_STENCIL_DW1_PITCH__SHIFT;

   if (ilo_dev_gen(dev) >= ILO_GEN(7.5))
      dw1 |= GEN75_STENCIL_DW1_STENCIL_BUFFER_ENABLE;

   dw2 = 0;
   /* offset to the level as Gen6 does not support mipmapped stencil */
   if (ilo_dev_gen(dev) == ILO_GEN(6)) {
      unsigned x, y;

      ilo_image_get_slice_pos(img, info->level, 0, &x, &y);
      ilo_image_pos_to_mem(img, x, y, &x, &y);
      dw2 |= ilo_image_mem_to_raw(img, x, y);
   }

   STATIC_ASSERT(ARRAY_SIZE(zs->stencil) >= 3);
   zs->stencil[0] = dw1;
   zs->stencil[1] = dw2;

   if (ilo_dev_gen(dev) >= ILO_GEN(8)) {
      uint32_t dw4;

      assert(img->walk_layer_height % 4 == 0);
      dw4 = (img->walk_layer_height / 4) << GEN8_STENCIL_DW4_QPITCH__SHIFT;

      zs->stencil[2] = dw4;
   }

   return true;
}

static bool
zs_set_gen6_null_3DSTATE_HIER_DEPTH_BUFFER(struct ilo_state_zs *zs,
                                           const struct ilo_dev *dev)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   STATIC_ASSERT(ARRAY_SIZE(zs->hiz) >= 3);
   zs->hiz[0] = 0;
   zs->hiz[1] = 0;
   if (ilo_dev_gen(dev) >= ILO_GEN(8))
      zs->hiz[2] = 0;

   return true;
}

static bool
zs_set_gen6_3DSTATE_HIER_DEPTH_BUFFER(struct ilo_state_zs *zs,
                                      const struct ilo_dev *dev,
                                      const struct ilo_state_zs_info *info)
{
   const struct ilo_image *img = info->z_img;
   uint32_t dw1, dw2;

   ILO_DEV_ASSERT(dev, 6, 8);

   assert(img->aux.bo_stride);

   dw1 = (img->aux.bo_stride - 1) << GEN6_HIZ_DW1_PITCH__SHIFT;

   dw2 = 0;
   /* offset to the level as Gen6 does not support mipmapped HiZ */
   if (ilo_dev_gen(dev) == ILO_GEN(6))
      dw2 |= img->aux.walk_lod_offsets[info->level];

   STATIC_ASSERT(ARRAY_SIZE(zs->hiz) >= 3);
   zs->hiz[0] = dw1;
   zs->hiz[1] = dw2;

   if (ilo_dev_gen(dev) >= ILO_GEN(8)) {
      uint32_t dw4;

      assert(img->aux.walk_layer_height % 4 == 0);
      dw4 = (img->aux.walk_layer_height / 4) << GEN8_HIZ_DW4_QPITCH__SHIFT;

      zs->hiz[2] = dw4;
   }

   return true;
}

bool
ilo_state_zs_init(struct ilo_state_zs *zs, const struct ilo_dev *dev,
                  const struct ilo_state_zs_info *info)
{
   bool ret = true;

   assert(ilo_is_zeroed(zs, sizeof(*zs)));

   if (info->z_img || info->s_img) {
      if (ilo_dev_gen(dev) >= ILO_GEN(7))
         ret &= zs_set_gen7_3DSTATE_DEPTH_BUFFER(zs, dev, info);
      else
         ret &= zs_set_gen6_3DSTATE_DEPTH_BUFFER(zs, dev, info);
   } else {
      ret &= zs_set_gen6_null_3DSTATE_DEPTH_BUFFER(zs, dev);
   }

   if (info->s_img)
      ret &= zs_set_gen6_3DSTATE_STENCIL_BUFFER(zs, dev, info);
   else
      ret &= zs_set_gen6_null_3DSTATE_STENCIL_BUFFER(zs, dev);

   if (info->z_img && info->hiz_vma)
      ret &= zs_set_gen6_3DSTATE_HIER_DEPTH_BUFFER(zs, dev, info);
   else
      ret &= zs_set_gen6_null_3DSTATE_HIER_DEPTH_BUFFER(zs, dev);

   zs->z_vma = info->z_vma;
   zs->s_vma = info->s_vma;
   zs->hiz_vma = info->hiz_vma;

   zs->z_readonly = info->z_readonly;
   zs->s_readonly = info->s_readonly;

   assert(ret);

   return ret;
}

bool
ilo_state_zs_init_for_null(struct ilo_state_zs *zs,
                           const struct ilo_dev *dev)
{
   struct ilo_state_zs_info info;

   memset(&info, 0, sizeof(info));
   info.type = GEN6_SURFTYPE_NULL;
   info.format = GEN6_ZFORMAT_D32_FLOAT;

   return ilo_state_zs_init(zs, dev, &info);
}

bool
ilo_state_zs_disable_hiz(struct ilo_state_zs *zs,
                         const struct ilo_dev *dev)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   /*
    * Separate stencil must be disabled simultaneously on Gen6.  We can make
    * it work when there is no stencil buffer, but it is probably not worth
    * it.
    */
   assert(ilo_dev_gen(dev) >= ILO_GEN(7));

   if (zs->hiz_vma) {
      zs->depth[0] &= ~GEN7_DEPTH_DW1_HIZ_ENABLE;
      zs_set_gen6_null_3DSTATE_HIER_DEPTH_BUFFER(zs, dev);
      zs->hiz_vma = NULL;
   }

   return true;
}
