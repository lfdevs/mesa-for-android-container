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
#include "ilo_state_sbe.h"

static bool
sbe_validate_gen8(const struct ilo_dev *dev,
                  const struct ilo_state_sbe_info *info)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   assert(info->attr_count <= ILO_STATE_SBE_MAX_ATTR_COUNT);

   assert(info->vue_read_base + info->vue_read_count <=
         info->cv_vue_attr_count);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 248:
    *
    *     "(Vertex URB Entry Read Length)
    *      Format: U5
    *      Range [1,16]
    *
    *      Specifies the amount of URB data read for each Vertex URB entry, in
    *      256-bit register increments.
    *
    *      Programming Notes
    *      It is UNDEFINED to set this field to 0 indicating no Vertex URB
    *      data to be read."
    *
    *     "(Vertex URB Entry Read Offset)
    *      Format: U6
    *      Range [0,63]
    *
    *      Specifies the offset (in 256-bit units) at which Vertex URB data is
    *      to be read from the URB."
    */
   assert(info->vue_read_base % 2 == 0 && info->vue_read_base <= 126);
   assert(info->vue_read_count <= 32);

   /*
    * From the Ivy Bridge PRM, volume 2 part 1, page 268:
    *
    *     "This field (Point Sprite Texture Coordinate Enable) must be
    *      programmed to 0 when non-point primitives are rendered."
    */
   if (ilo_dev_gen(dev) < ILO_GEN(7.5) && info->point_sprite_enables)
      assert(info->cv_is_point);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 246:
    *
    *     "(Number of SF Output Attributes) 33-48: Specifies 17-32 attributes
    *      (# attributes = field value - 16). Swizzling performed on
    *      Attributes 16-31 (as required) only. Attributes 0-15 passed through
    *      unmodified.
    *
    *      Note :
    *
    *      Attribute n Component Override and Constant Source states apply to
    *      Attributes 16-31 (as required) instead of Attributes 0-15. E.g.,
    *      this allows an Attribute 16-31 component to be overridden with the
    *      PrimitiveID value.
    *
    *      Attribute n WrapShortest Enables still apply to Attributes 0-15.
    *
    *      Attribute n Swizzle Select and Attribute n Source Attribute states
    *      are ignored and none of the swizzling functions available through
    *      these controls are performed."
    *
    * From the Sandy Bridge PRM, volume 2 part 1, page 247:
    *
    *     "This bit (Attribute Swizzle Enable) controls the use of the
    *      Attribute n Swizzle Select and Attribute n Source Attribute fields
    *      only. If ENABLED, those fields are used as described below. If
    *      DISABLED, attributes are copied from their corresponding source
    *      attributes, for the purposes of Swizzle Select only.
    *
    *      Note that the following fields are unaffected by this bit, and are
    *      therefore always used to control their respective fields:
    *      Attribute n Component Override X/Y/Z/W
    *      Attribute n Constant Source
    *      Attribute n WrapShortest Enables"
    *
    * From the Ivy Bridge PRM, volume 2 part 1, page 264:
    *
    *     "When Attribute Swizzle Enable is ENABLED, this bit (Attribute
    *      Swizzle Control Mode) controls whether attributes 0-15 or 16-31 are
    *      subject to the following swizzle controls:
    *
    *      - Attribute n Component Override X/Y/Z/W
    *      - Attribute n Constant Source
    *      - Attribute n Swizzle Select
    *      - Attribute n Source Attribute
    *      - Attribute n Wrap Shortest Enables"
    *
    *     "SWIZ_16_31... Only valid when 16 or more attributes are output."
    */
   assert(info->swizzle_count <= ILO_STATE_SBE_MAX_SWIZZLE_COUNT);
   if (info->swizzle_16_31) {
      assert(ilo_dev_gen(dev) >= ILO_GEN(7) &&
             info->swizzle_enable &&
             info->attr_count > 16);
   }

   return true;
}

static uint8_t
sbe_get_gen8_min_read_count(const struct ilo_dev *dev,
                            const struct ilo_state_sbe_info *info)
{
   uint8_t min_count = 0;

   ILO_DEV_ASSERT(dev, 6, 8);

   /* minimum read count for non-swizzled attributes */
   if (!info->swizzle_enable || info->swizzle_count < info->attr_count) {
      if (info->swizzle_16_31 && info->swizzle_count + 16 == info->attr_count)
         min_count = 16;
      else
         min_count = info->attr_count;
   }

   if (info->swizzle_enable) {
      uint8_t i;

      for (i = 0; i < info->swizzle_count; i++) {
         const struct ilo_state_sbe_swizzle_info *swizzle =
            &info->swizzles[i];
         bool inputattr_facing;

         switch (swizzle->attr_select) {
         case GEN6_INPUTATTR_FACING:
         case GEN6_INPUTATTR_FACING_W:
            inputattr_facing = true;
            break;
         default:
            inputattr_facing = false;
            break;
         }

         if (min_count < swizzle->attr + inputattr_facing + 1)
            min_count = swizzle->attr + inputattr_facing + 1;
      }
   }

   return min_count;
}

static uint8_t
sbe_get_gen8_read_length(const struct ilo_dev *dev,
                         const struct ilo_state_sbe_info *info)
{
   uint8_t read_len;

   ILO_DEV_ASSERT(dev, 6, 8);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 248:
    *
    *     "(Vertex URB Entry Read Length)
    *      This field should be set to the minimum length required to read the
    *      maximum source attribute. The maximum source attribute is indicated
    *      by the maximum value of the enabled Attribute # Source Attribute if
    *      Attribute Swizzle Enable is set, Number of Output Attributes -1 if
    *      enable is not set.
    *      read_length = ceiling((max_source_attr+1)/2)
    *
    *      [errata] Corruption/Hang possible if length programmed larger than
    *      recommended"
    */
   if (info->has_min_read_count) {
      read_len = info->vue_read_count;
      assert(read_len == sbe_get_gen8_min_read_count(dev, info));
   } else {
      read_len = sbe_get_gen8_min_read_count(dev, info);
      assert(read_len <= info->vue_read_count);
   }

   /*
    * In pairs.  URB entries are aligned to 1024-bits or 512-bits.  There is
    * no need to worry about reading past entries.
    */
   read_len = (read_len + 1) / 2;
   if (!read_len)
      read_len = 1;

   return read_len;
}

static bool
sbe_set_gen8_3DSTATE_SBE(struct ilo_state_sbe *sbe,
                         const struct ilo_dev *dev,
                         const struct ilo_state_sbe_info *info)
{
   uint8_t vue_read_offset, vue_read_len;
   uint8_t attr_count;
   uint32_t dw1, dw2, dw3;

   ILO_DEV_ASSERT(dev, 6, 8);

   if (!sbe_validate_gen8(dev, info))
      return false;

   vue_read_offset = info->vue_read_base / 2;
   vue_read_len = sbe_get_gen8_read_length(dev, info);

   attr_count = info->attr_count;
   if (ilo_dev_gen(dev) == ILO_GEN(6) && info->swizzle_16_31)
      attr_count += 16;

   dw1 = attr_count << GEN7_SBE_DW1_ATTR_COUNT__SHIFT |
         vue_read_len << GEN7_SBE_DW1_URB_READ_LEN__SHIFT;

   if (ilo_dev_gen(dev) >= ILO_GEN(8)) {
      dw1 |= GEN8_SBE_DW1_FORCE_URB_READ_LEN |
             GEN8_SBE_DW1_FORCE_URB_READ_OFFSET |
             vue_read_offset << GEN8_SBE_DW1_URB_READ_OFFSET__SHIFT;
   } else {
      dw1 |= vue_read_offset << GEN7_SBE_DW1_URB_READ_OFFSET__SHIFT;
   }

   if (ilo_dev_gen(dev) >= ILO_GEN(7) && info->swizzle_16_31)
      dw1 |= GEN7_SBE_DW1_ATTR_SWIZZLE_16_31;

   if (info->swizzle_enable)
      dw1 |= GEN7_SBE_DW1_ATTR_SWIZZLE_ENABLE;

   dw1 |= (info->point_sprite_origin_lower_left) ?
      GEN7_SBE_DW1_POINT_SPRITE_TEXCOORD_LOWERLEFT :
      GEN7_SBE_DW1_POINT_SPRITE_TEXCOORD_UPPERLEFT;

   dw2 = info->point_sprite_enables;
   dw3 = info->const_interp_enables;

   STATIC_ASSERT(ARRAY_SIZE(sbe->sbe) >= 3);
   sbe->sbe[0] = dw1;
   sbe->sbe[1] = dw2;
   sbe->sbe[2] = dw3;

   return true;
}

static bool
sbe_set_gen8_3DSTATE_SBE_SWIZ(struct ilo_state_sbe *sbe,
                              const struct ilo_dev *dev,
                              const struct ilo_state_sbe_info *info)
{
   uint16_t swiz[ILO_STATE_SBE_MAX_SWIZZLE_COUNT];
   uint8_t i;

   ILO_DEV_ASSERT(dev, 6, 8);

   for (i = 0; i < info->swizzle_count; i++) {
      const struct ilo_state_sbe_swizzle_info *swizzle = &info->swizzles[i];

      /* U5 */
      assert(swizzle->attr < 32);
      swiz[i] = swizzle->attr_select << GEN8_SBE_SWIZ_SWIZZLE_SELECT__SHIFT |
                swizzle->attr << GEN8_SBE_SWIZ_SRC_ATTR__SHIFT;

      if (swizzle->force_zeros) {
         swiz[i] |= GEN8_SBE_SWIZ_CONST_OVERRIDE_W |
                    GEN8_SBE_SWIZ_CONST_OVERRIDE_Z |
                    GEN8_SBE_SWIZ_CONST_OVERRIDE_Y |
                    GEN8_SBE_SWIZ_CONST_OVERRIDE_X |
                    GEN8_SBE_SWIZ_CONST_0000;
      }
   }

   for (; i < ARRAY_SIZE(swiz); i++) {
      swiz[i] = GEN6_INPUTATTR_NORMAL << GEN8_SBE_SWIZ_SWIZZLE_SELECT__SHIFT |
                i << GEN8_SBE_SWIZ_SRC_ATTR__SHIFT;
   }

   STATIC_ASSERT(sizeof(sbe->swiz) == sizeof(swiz));
   memcpy(sbe->swiz, swiz, sizeof(swiz));

   return true;
}

bool
ilo_state_sbe_init(struct ilo_state_sbe *sbe,
                   const struct ilo_dev *dev,
                   const struct ilo_state_sbe_info *info)
{
   assert(ilo_is_zeroed(sbe, sizeof(*sbe)));
   return ilo_state_sbe_set_info(sbe, dev, info);
}

bool
ilo_state_sbe_init_for_rectlist(struct ilo_state_sbe *sbe,
                                const struct ilo_dev *dev,
                                uint8_t read_base,
                                uint8_t read_count)
{
   struct ilo_state_sbe_info info;

   memset(&info, 0, sizeof(info));
   info.attr_count = read_count;
   info.cv_vue_attr_count = read_base + read_count;
   info.vue_read_base = read_base;
   info.vue_read_count = read_count;
   info.has_min_read_count = true;

   return ilo_state_sbe_set_info(sbe, dev, &info);
}

bool
ilo_state_sbe_set_info(struct ilo_state_sbe *sbe,
                       const struct ilo_dev *dev,
                       const struct ilo_state_sbe_info *info)
{
   bool ret = true;

   ILO_DEV_ASSERT(dev, 6, 8);

   ret &= sbe_set_gen8_3DSTATE_SBE(sbe, dev, info);
   ret &= sbe_set_gen8_3DSTATE_SBE_SWIZ(sbe, dev, info);

   assert(ret);

   return true;
}
