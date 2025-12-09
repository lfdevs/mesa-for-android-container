/*
 * Copyright (C) 2008 VMware, Inc.
 * Copyright (C) 2014 Broadcom
 * Copyright (C) 2018-2019 Alyssa Rosenzweig
 * Copyright (C) 2019-2020 Collabora, Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __PAN_IMAGE_H
#define __PAN_IMAGE_H

#include "genxml/gen_macros.h"

#include <stdbool.h>
#include "util/format/u_format.h"
#include "pan_format.h"
#include "pan_layout.h"
#include "pan_mod.h"
#include "pan_props.h"

#include "kmod/pan_kmod.h"

#include "util/log.h"

#ifdef __cplusplus
extern "C" {
#endif

struct pan_mod_handler;

struct pan_image_plane {
   struct pan_image_layout layout;
   uint64_t base;
};

struct pan_image {
   struct pan_image_props props;
   const struct pan_mod_handler *mod_handler;
   struct pan_image_plane *planes[MAX_IMAGE_PLANES];
};

struct pan_image_plane_ref {
   struct pan_image *image;
   uint32_t plane_idx;
};

struct pan_image_view {
   /* Format, dimension and sample count of the view might differ from
    * those of the image (2D view of a 3D image surface for instance).
    */
   enum pipe_format format;
   enum mali_texture_dimension dim;
   unsigned first_level, last_level;
   unsigned first_layer, last_layer;
   unsigned char swizzle[4];

   /* planes 1 and 2 are NULL for single plane formats */
   struct pan_image_plane_ref planes[MAX_IMAGE_PLANES];

   /* If EXT_multisampled_render_to_texture is used, this may be
    * greater than image->layout.nr_samples. */
   unsigned nr_samples;

   struct {
      unsigned narrow;
      unsigned hdr;
   } astc;
};

static inline struct pan_image_plane_ref
pan_image_view_get_plane(const struct pan_image_view *iview, uint32_t idx)
{
   if (idx >= ARRAY_SIZE(iview->planes))
      return (struct pan_image_plane_ref){0};

   return iview->planes[idx];
}

static inline unsigned
pan_image_view_get_plane_mask(const struct pan_image_view *iview)
{
   unsigned mask = 0;

   for (unsigned i = 0; i < ARRAY_SIZE(iview->planes); i++) {
      if (iview->planes[i].image)
         mask |= BITFIELD_BIT(i);
   }

   return mask;
}

static inline unsigned
pan_image_view_get_first_plane_idx(const struct pan_image_view *iview)
{
   unsigned mask = pan_image_view_get_plane_mask(iview);

   assert(mask);
   return ffs(mask) - 1;
}

static inline struct pan_image_plane_ref
pan_image_view_get_first_plane(const struct pan_image_view *iview)
{
   unsigned first_plane_idx = pan_image_view_get_first_plane_idx(iview);
   return pan_image_view_get_plane(iview, first_plane_idx);
}

static inline uint32_t
pan_image_view_get_nr_samples(const struct pan_image_view *iview)
{
   const struct pan_image_plane_ref pref = pan_image_view_get_first_plane(iview);

   if (!pref.image)
      return 0;

   return pref.image->props.nr_samples;
}

static inline const struct pan_image_plane_ref
pan_image_view_get_color_plane(const struct pan_image_view *iview)
{
   /* We only support rendering to plane 0 */
   assert(pan_image_view_get_plane(iview, 1).image == NULL);
   return pan_image_view_get_plane(iview, 0);
}

static inline bool
pan_image_view_has_crc(const struct pan_image_view *iview)
{
   const struct pan_image_plane_ref p = pan_image_view_get_color_plane(iview);

   if (!p.image)
      return false;

   return p.image->props.crc;
}

static inline struct pan_image_plane_ref
pan_image_view_get_s_plane(const struct pan_image_view *iview)
{
   ASSERTED const struct util_format_description *fdesc =
      util_format_description(iview->format);
   assert(util_format_has_stencil(fdesc));

   /* In case of multiplanar depth/stencil, the stencil is always on
    * plane 1. Combined depth/stencil only has one plane, so depth
    * will be on plane 0 in either case.
    */
   const struct pan_image_plane_ref pref =
      iview->planes[1].image ? iview->planes[1] : iview->planes[0];

   assert(pref.image);
   fdesc = util_format_description(pref.image->props.format);
   assert(util_format_has_stencil(fdesc));
   return pref;
}

static inline struct pan_image_plane_ref
pan_image_view_get_zs_plane(const struct pan_image_view *iview)
{
   assert(util_format_is_depth_or_stencil(iview->format));

   /* Depth or combined depth-stencil is always on plane 0. */
   return pan_image_view_get_plane(iview, 0);
}

static inline void
pan_image_view_check(const struct pan_image_view *iview)
{
#ifndef NDEBUG
   unsigned nplanes = util_format_get_num_planes(iview->format);
   struct pan_image_plane_ref pref;

   for (unsigned i = 0; i < nplanes; i++) {
      if (util_format_is_depth_or_stencil(iview->format)) {
         const struct util_format_description *fdesc =
            util_format_description(iview->format);

         if (util_format_has_stencil(fdesc))
            pref = pan_image_view_get_s_plane(iview);
         else
            pref = pan_image_view_get_zs_plane(iview);
      } else {
         pref = iview->planes[i];
      }

      /* Make sure we have an image and the plane we point to exists. */
      assert(pref.image);
      assert(pref.plane_idx <
             util_format_get_num_planes(pref.image->props.format));

      enum pipe_format view_format =
         util_format_get_plane_format(iview->format, i);
      enum pipe_format img_format =
         util_format_get_plane_format(pref.image->props.format, pref.plane_idx);

      /* View-based pixel re-interpretation only allowed if the formats
       * blocksize match. */
      assert(util_format_get_blocksize(view_format) ==
             util_format_get_blocksize(img_format));
   }
#endif
}

static inline uint64_t
pan_image_mip_level_size(const struct pan_image *image, unsigned plane_idx,
                         unsigned mip_level)
{
   assert(plane_idx < ARRAY_SIZE(image->planes) &&
          plane_idx < util_format_get_num_planes(image->props.format));
   assert(mip_level < image->props.nr_slices);
   assert(image->planes[plane_idx]);

   uint64_t size = image->planes[plane_idx]->layout.slices[mip_level].size_B;

   /* If this is an array, we need to cover the whole array. */
   if (image->props.array_size > 1) {
      size += image->planes[plane_idx]->layout.array_stride_B *
              (image->props.array_size - 1);
   }

   return size;
}

static inline uint32_t
pan_image_get_wsi_row_pitch(const struct pan_image *image, unsigned plane_idx,
                            unsigned mip_level)
{
   assert(image->mod_handler);
   assert(image->mod_handler->get_wsi_row_pitch);
   assert(plane_idx < ARRAY_SIZE(image->planes) &&
          plane_idx < util_format_get_num_planes(image->props.format));
   assert(image->planes[plane_idx]);

   return image->mod_handler->get_wsi_row_pitch(image, plane_idx, mip_level);
}

static inline uint64_t
pan_image_get_wsi_offset(const struct pan_image *image, unsigned plane_idx,
                         unsigned mip_level)
{
   assert(plane_idx < ARRAY_SIZE(image->planes) &&
          plane_idx < util_format_get_num_planes(image->props.format));
   assert(mip_level < image->props.nr_slices);
   assert(image->planes[plane_idx]);

   return image->planes[plane_idx]->layout.slices[mip_level].offset_B;
}

bool pan_image_layout_init(
   unsigned arch, struct pan_image *image, unsigned plane_idx,
   const struct pan_image_layout_constraints *explicit_layout_constraints);

struct pan_image_usage {
   /* PAN_BIND_xxx flags. */
   uint32_t bind;

   /* Image filled directly from the CPU. */
   bool host_copy;

   /* Image frequently updated with host data. */
   bool frequent_host_updates;

   /* Legacy scanout image. */
   bool legacy_scanout;

   /* Image created by WSI. */
   bool wsi;
};

static inline enum pan_mod_support
pan_image_test_props(const struct pan_kmod_dev_props *dprops,
                     const struct pan_image_props *iprops,
                     const struct pan_image_usage *iusage)
{
   const unsigned arch = pan_arch(dprops->gpu_id);
   struct pan_image image = {
      .props = *iprops,
      .mod_handler = pan_mod_get_handler(arch, iprops->modifier),
   };

   if (!image.mod_handler)
      return PAN_MOD_NOT_SUPPORTED;

   enum pan_mod_support ret =
      image.mod_handler->test_props(dprops, &image.props, iusage);
   if (ret == PAN_MOD_NOT_SUPPORTED)
      return ret;

   /* Now make sure the layout can be properly initialized on all planes. */
   uint32_t plane_count = util_format_get_num_planes(image.props.format);
   for (uint32_t p = 0; p < plane_count; p++) {
      struct pan_image_plane plane;

      memset(&plane, 0, sizeof(plane));
      image.planes[p] = &plane;

      if (!pan_image_layout_init(arch, &image, p, NULL))
         return PAN_MOD_NOT_SUPPORTED;
   }

   return ret;
}

static inline bool
pan_image_test_modifier_with_format(const struct pan_kmod_dev_props *dprops,
                                    uint64_t modifier, enum pipe_format format)
{
   /* To check if a <modifier,format> pair is supported, we define the smallest
    * possible 2D image (or 3D image if this is a 3D compressed format). */
   const struct pan_image_props iprops = {
      .modifier = modifier,
      .format = format,
      .extent_px = {
            .width = util_format_get_blockwidth(format),
            .height = util_format_get_blockheight(format),
            .depth = util_format_get_blockdepth(format),
      },
      .nr_samples = 1,
      .dim = util_format_get_blockdepth(format) > 1 ? MALI_TEXTURE_DIMENSION_3D
                                                    : MALI_TEXTURE_DIMENSION_2D,
      .nr_slices = 1,
      .array_size = 1,
   };

   return pan_image_test_props(dprops, &iprops, NULL) != PAN_MOD_NOT_SUPPORTED;
}

#ifdef __cplusplus
} /* extern C */
#endif

#endif
