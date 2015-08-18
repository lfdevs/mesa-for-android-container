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
#include "ilo_state_viewport.h"

static void
viewport_matrix_get_gen6_guardband(const struct ilo_dev *dev,
                                   const struct ilo_state_viewport_matrix_info *mat,
                                   float *min_gbx, float *max_gbx,
                                   float *min_gby, float *max_gby)
{
   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 234:
    *
    *     "Per-Device Guardband Extents
    *
    *       - Supported X,Y ScreenSpace "Guardband" Extent: [-16K,16K-1]
    *       - Maximum Post-Clamp Delta (X or Y): 16K"
    *
    *     "In addition, in order to be correctly rendered, objects must have a
    *      screenspace bounding box not exceeding 8K in the X or Y direction.
    *      This additional restriction must also be comprehended by software,
    *      i.e., enforced by use of clipping."
    *
    * From the Ivy Bridge PRM, volume 2 part 1, page 248:
    *
    *     "Per-Device Guardband Extents
    *
    *       - Supported X,Y ScreenSpace "Guardband" Extent: [-32K,32K-1]
    *       - Maximum Post-Clamp Delta (X or Y): N/A"
    *
    *     "In addition, in order to be correctly rendered, objects must have a
    *      screenspace bounding box not exceeding 8K in the X or Y direction.
    *      This additional restriction must also be comprehended by software,
    *      i.e., enforced by use of clipping."
    *
    * Combined, the bounding box of any object can not exceed 8K in both
    * width and height.
    *
    * Below we set the guardband as a squre of length 8K, centered at where
    * the viewport is.  This makes sure all objects passing the GB test are
    * valid to the renderer, and those failing the XY clipping have a
    * better chance of passing the GB test.
    */
   const int max_extent = (ilo_dev_gen(dev) >= ILO_GEN(7)) ? 32768 : 16384;
   const int half_len = 8192 / 2;
   int center_x = (int) mat->translate[0];
   int center_y = (int) mat->translate[1];
   float scale_x, scale_y;

   ILO_DEV_ASSERT(dev, 6, 8);

   /* make sure the guardband is within the valid range */
   if (center_x - half_len < -max_extent)
      center_x = -max_extent + half_len;
   else if (center_x + half_len > max_extent - 1)
      center_x = max_extent - half_len;

   if (center_y - half_len < -max_extent)
      center_y = -max_extent + half_len;
   else if (center_y + half_len > max_extent - 1)
      center_y = max_extent - half_len;

   scale_x = fabsf(mat->scale[0]);
   scale_y = fabsf(mat->scale[1]);
   /*
    * From the Haswell PRM, volume 2d, page 292-293:
    *
    *     "Note: Minimum allowed value for this field (X/Y Min Clip Guardband)
    *      is -16384."
    *
    *     "Note: Maximum allowed value for this field (X/Y Max Clip Guardband)
    *      is 16383."
    *
    * Avoid small scales.
    */
   if (scale_x < 1.0f)
      scale_x = 1.0f;
   if (scale_y < 1.0f)
      scale_y = 1.0f;

   /* in NDC space */
   *min_gbx = ((float) (center_x - half_len) - mat->translate[0]) / scale_x;
   *max_gbx = ((float) (center_x + half_len) - mat->translate[0]) / scale_x;
   *min_gby = ((float) (center_y - half_len) - mat->translate[1]) / scale_y;
   *max_gby = ((float) (center_y + half_len) - mat->translate[1]) / scale_y;
}

static void
viewport_matrix_get_extent(const struct ilo_state_viewport_matrix_info *mat,
                           int axis, float *min, float *max)
{
   const float scale_abs = fabsf(mat->scale[axis]);

   *min = -1.0f * scale_abs + mat->translate[axis];
   *max =  1.0f * scale_abs + mat->translate[axis];
}

static bool
viewport_matrix_set_gen7_SF_CLIP_VIEWPORT(struct ilo_state_viewport *vp,
                                          const struct ilo_dev *dev,
                                          const struct ilo_state_viewport_matrix_info *matrices,
                                          uint8_t count)
{
   uint8_t i;

   ILO_DEV_ASSERT(dev, 6, 8);

   for (i = 0; i < count; i++) {
      const struct ilo_state_viewport_matrix_info *mat = &matrices[i];
      float min_gbx, max_gbx, min_gby, max_gby;
      uint32_t dw[16];

      viewport_matrix_get_gen6_guardband(dev, mat,
            &min_gbx, &max_gbx, &min_gby, &max_gby);

      dw[0] = fui(mat->scale[0]);
      dw[1] = fui(mat->scale[1]);
      dw[2] = fui(mat->scale[2]);
      dw[3] = fui(mat->translate[0]);
      dw[4] = fui(mat->translate[1]);
      dw[5] = fui(mat->translate[2]);
      dw[6] = 0;
      dw[7] = 0;

      dw[8] = fui(min_gbx);
      dw[9] = fui(max_gbx);
      dw[10] = fui(min_gby);
      dw[11] = fui(max_gby);

      if (ilo_dev_gen(dev) >= ILO_GEN(8)) {
         float min_x, max_x, min_y, max_y;

         viewport_matrix_get_extent(mat, 0, &min_x, &max_x);
         viewport_matrix_get_extent(mat, 1, &min_y, &max_y);

         dw[12] = fui(min_x);
         dw[13] = fui(max_x - 1.0f);
         dw[14] = fui(min_y);
         dw[15] = fui(max_y - 1.0f);
      } else {
         dw[12] = 0;
         dw[13] = 0;
         dw[14] = 0;
         dw[15] = 0;
      }

      STATIC_ASSERT(ARRAY_SIZE(vp->sf_clip[i]) >= 16);
      memcpy(vp->sf_clip[i], dw, sizeof(dw));
   }

   return true;
}

static bool
viewport_matrix_set_gen6_CC_VIEWPORT(struct ilo_state_viewport *vp,
                                     const struct ilo_dev *dev,
                                     const struct ilo_state_viewport_matrix_info *matrices,
                                     uint8_t count)
{
   uint8_t i;

   ILO_DEV_ASSERT(dev, 6, 8);

   for (i = 0; i < count; i++) {
      const struct ilo_state_viewport_matrix_info *mat = &matrices[i];
      float min_z, max_z;

      viewport_matrix_get_extent(mat, 2, &min_z, &max_z);

      STATIC_ASSERT(ARRAY_SIZE(vp->cc[i]) >= 2);
      vp->cc[i][0] = fui(min_z);
      vp->cc[i][1] = fui(max_z);
   }

   return true;
}

static bool
viewport_scissor_set_gen6_SCISSOR_RECT(struct ilo_state_viewport *vp,
                                       const struct ilo_dev *dev,
                                       const struct ilo_state_viewport_scissor_info *scissors,
                                       uint8_t count)
{
   const uint16_t max_size = (ilo_dev_gen(dev) >= ILO_GEN(7)) ? 16384 : 8192;
   uint8_t i;

   ILO_DEV_ASSERT(dev, 6, 8);

   for (i = 0; i < count; i++) {
      const struct ilo_state_viewport_scissor_info *scissor = &scissors[i];
      uint16_t min_x, min_y, max_x, max_y;
      uint32_t dw0, dw1;

      min_x = (scissor->min_x < max_size) ? scissor->min_x : max_size - 1;
      min_y = (scissor->min_y < max_size) ? scissor->min_y : max_size - 1;
      max_x = (scissor->max_x < max_size) ? scissor->max_x : max_size - 1;
      max_y = (scissor->max_y < max_size) ? scissor->max_y : max_size - 1;

      dw0 = min_y << GEN6_SCISSOR_DW0_MIN_Y__SHIFT |
            min_x << GEN6_SCISSOR_DW0_MIN_X__SHIFT;
      dw1 = max_y << GEN6_SCISSOR_DW1_MAX_Y__SHIFT |
            max_x << GEN6_SCISSOR_DW1_MAX_X__SHIFT;

      STATIC_ASSERT(ARRAY_SIZE(vp->scissor[i]) >= 2);
      vp->scissor[i][0] = dw0;
      vp->scissor[i][1] = dw1;
   }

   return true;
}

bool
ilo_state_viewport_init(struct ilo_state_viewport *vp,
                        const struct ilo_dev *dev,
                        const struct ilo_state_viewport_info *info)
{
   const size_t elem_size = ilo_state_viewport_data_size(dev, 1);

   assert(ilo_is_zeroed(vp, sizeof(*vp)));
   assert(ilo_is_zeroed(info->data, info->data_size));

   vp->data = info->data;

   if (info->data_size / elem_size < ILO_STATE_VIEWPORT_MAX_COUNT)
      vp->array_size = info->data_size / elem_size;
   else
      vp->array_size = ILO_STATE_VIEWPORT_MAX_COUNT;

   return ilo_state_viewport_set_params(vp, dev, &info->params, false);
}

bool
ilo_state_viewport_init_data_only(struct ilo_state_viewport *vp,
                                  const struct ilo_dev *dev,
                                  void *data, size_t data_size)
{
   struct ilo_state_viewport_info info;

   memset(&info, 0, sizeof(info));
   info.data = data;
   info.data_size = data_size;

   return ilo_state_viewport_init(vp, dev, &info);
}

bool
ilo_state_viewport_init_for_rectlist(struct ilo_state_viewport *vp,
                                     const struct ilo_dev *dev,
                                     void *data, size_t data_size)
{
   struct ilo_state_viewport_info info;
   struct ilo_state_viewport_matrix_info mat;
   struct ilo_state_viewport_scissor_info sci;

   memset(&info, 0, sizeof(info));
   memset(&mat, 0, sizeof(mat));
   memset(&sci, 0, sizeof(sci));

   info.data = data;
   info.data_size = data_size;
   info.params.matrices = &mat;
   info.params.scissors = &sci;
   info.params.count = 1;

   mat.scale[0] = 1.0f;
   mat.scale[1] = 1.0f;
   mat.scale[2] = 1.0f;

   return ilo_state_viewport_init(vp, dev, &info);
}

static void
viewport_set_count(struct ilo_state_viewport *vp,
                   const struct ilo_dev *dev,
                   uint8_t count)
{
   assert(count <= vp->array_size);

   vp->count = count;
   vp->sf_clip = (uint32_t (*)[16]) vp->data;
   vp->cc =      (uint32_t (*)[ 2]) (vp->sf_clip + count);
   vp->scissor = (uint32_t (*)[ 2]) (vp->cc + count);
}

bool
ilo_state_viewport_set_params(struct ilo_state_viewport *vp,
                              const struct ilo_dev *dev,
                              const struct ilo_state_viewport_params_info *params,
                              bool scissors_only)
{
   bool ret = true;

   if (scissors_only) {
      assert(vp->count == params->count);

      ret &= viewport_scissor_set_gen6_SCISSOR_RECT(vp, dev,
            params->scissors, params->count);
   } else {
      viewport_set_count(vp, dev, params->count);

      ret &= viewport_matrix_set_gen7_SF_CLIP_VIEWPORT(vp, dev,
            params->matrices, params->count);
      ret &= viewport_matrix_set_gen6_CC_VIEWPORT(vp, dev,
            params->matrices, params->count);
      ret &= viewport_scissor_set_gen6_SCISSOR_RECT(vp, dev,
            params->scissors, params->count);
   }

   assert(ret);

   return ret;
}

void
ilo_state_viewport_full_delta(const struct ilo_state_viewport *vp,
                              const struct ilo_dev *dev,
                              struct ilo_state_viewport_delta *delta)
{
   delta->dirty = ILO_STATE_VIEWPORT_SF_CLIP_VIEWPORT |
                  ILO_STATE_VIEWPORT_CC_VIEWPORT |
                  ILO_STATE_VIEWPORT_SCISSOR_RECT;
}

void
ilo_state_viewport_get_delta(const struct ilo_state_viewport *vp,
                             const struct ilo_dev *dev,
                             const struct ilo_state_viewport *old,
                             struct ilo_state_viewport_delta *delta)
{
   const size_t sf_clip_size = sizeof(vp->sf_clip[0]) * vp->count;
   const size_t cc_size = sizeof(vp->cc[0]) * vp->count;
   const size_t scissor_size = sizeof(vp->scissor[0]) * vp->count;

   /* no shallow copying */
   assert(vp->data != old->data);

   if (vp->count != old->count) {
      ilo_state_viewport_full_delta(vp, dev, delta);
      return;
   }

   delta->dirty = 0;

   if (memcmp(vp->sf_clip, old->sf_clip, sf_clip_size))
      delta->dirty |= ILO_STATE_VIEWPORT_SF_CLIP_VIEWPORT;

   if (memcmp(vp->cc, old->cc, cc_size))
      delta->dirty |= ILO_STATE_VIEWPORT_CC_VIEWPORT;

   if (memcmp(vp->scissor, old->scissor, scissor_size))
      delta->dirty |= ILO_STATE_VIEWPORT_SCISSOR_RECT;
}
