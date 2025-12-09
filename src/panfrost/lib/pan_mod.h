/*
 * Copyright (C) 2025 Collabora, Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __PAN_MOD_H
#define __PAN_MOD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "util/format/u_format.h"

#include "pan_layout.h"

struct pan_fb_info;
struct pan_image;
struct pan_image_view;
struct pan_image_usage;
struct pan_kmod_dev_props;
struct pan_mod_handler;

enum pan_mod_support {
   PAN_MOD_NOT_SUPPORTED = 0,
   PAN_MOD_NOT_OPTIMAL,
   PAN_MOD_OPTIMAL,
};

struct pan_mod_handler {
   bool (*match)(uint64_t mod);

   /* Used to check if a set of image properties is valid. Passing a NULL iusage
    * is valid and means "optimal set of usage for this mod". This implies
    * that some non-supported cases can't be detected or can be reported as
    * optimal when specific usage flags would report it non-optimal.
    */
   enum pan_mod_support (*test_props)(const struct pan_kmod_dev_props *dprops,
                                      const struct pan_image_props *iprops,
                                      const struct pan_image_usage *iusage);

   /* Optional method used to initialize modifier-specific per-plane layout
    * data. This is called before init_slice_layout().
    */
   bool (*init_plane_layout)(const struct pan_image_props *props,
                             unsigned plane_idx,
                             struct pan_image_layout *plane_layout);

   bool (*init_slice_layout)(
      const struct pan_image_props *props, unsigned plane_idx,
      struct pan_image_extent mip_extent_px,
      const struct pan_image_layout_constraints *layout_constraints,
      struct pan_image_slice_layout *slice);
   uint32_t (*get_wsi_row_pitch)(const struct pan_image *image,
                                 unsigned plane_idx, unsigned mip_level);

   void (*emit_tex_payload_entry)(const struct pan_image_view *iview,
                                  unsigned mip_level, unsigned layer_or_z_slice,
                                  unsigned sample, void **payload);

   void (*emit_color_attachment)(const struct pan_fb_info *fb, unsigned rt_idx,
                                 unsigned layer_or_z_slice,
                                 unsigned cbuf_offset, void *payload);
   void (*emit_zs_attachment)(const struct pan_fb_info *fb,
                              unsigned layer_or_z_slice, void *payload);
   void (*emit_s_attachment)(const struct pan_fb_info *fb,
                             unsigned layer_or_z_slice, void *payload);
};

#ifdef PAN_ARCH
const struct pan_mod_handler *GENX(pan_mod_get_handler)(uint64_t modifier);

static inline const struct pan_mod_handler *
pan_mod_get_handler(unsigned arch, uint64_t modifier)
{
   assert(arch == PAN_ARCH);
   return GENX(pan_mod_get_handler)(modifier);
}
#else
const struct pan_mod_handler *pan_mod_get_handler_v4(uint64_t modifier);
const struct pan_mod_handler *pan_mod_get_handler_v5(uint64_t modifier);
const struct pan_mod_handler *pan_mod_get_handler_v6(uint64_t modifier);
const struct pan_mod_handler *pan_mod_get_handler_v7(uint64_t modifier);
const struct pan_mod_handler *pan_mod_get_handler_v9(uint64_t modifier);
const struct pan_mod_handler *pan_mod_get_handler_v10(uint64_t modifier);
const struct pan_mod_handler *pan_mod_get_handler_v12(uint64_t modifier);
const struct pan_mod_handler *pan_mod_get_handler_v13(uint64_t modifier);

static inline const struct pan_mod_handler *
pan_mod_get_handler(unsigned arch, uint64_t modifier)
{
   switch (arch) {
   case 4:
      return pan_mod_get_handler_v4(modifier);
   case 5:
      return pan_mod_get_handler_v5(modifier);
   case 6:
      return pan_mod_get_handler_v6(modifier);
   case 7:
      return pan_mod_get_handler_v7(modifier);
   case 9:
      return pan_mod_get_handler_v9(modifier);
   case 10:
      return pan_mod_get_handler_v10(modifier);
   case 12:
      return pan_mod_get_handler_v12(modifier);
   case 13:
      return pan_mod_get_handler_v13(modifier);
   default:
      UNREACHABLE("Unsupported arch");
   }
}
#endif

#ifdef __cplusplus
} /* extern C */
#endif

#endif
